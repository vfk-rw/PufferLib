/* header-only impl file for airsim RL env. */
#ifndef AIRSIM_H
#define AIRSIM_H

#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "raylib.h"
#include <stdio.h> // Added to declare printf and fprintf
#include <string.h>
#include "yaml.h"

#define MAX_THREATS 10
#define MAX_LASERS 5
#define PI 3.14159265358979323846f
// #define DEBUG_TERMINAL
#define DEBUG_PRINT
#define DEBUG_PRONAV // Add this line
#define LOG_BUFFER_SIZE 1024
#define MAX_SEEKER_ERROR_SCALE 50.0f // Make error much larger for visibility
#define RAD2DEG_SAFE 57.2957795131f  // 180/pi
#define DEG2RAD_SAFE 0.0174532925f   // pi/180

#include "log.h"

// Forward declare log functions
LogBuffer *allocate_logbuffer(int size);
void free_logbuffer(LogBuffer *buffer);
void add_log(LogBuffer *logs, Log *log);
Log aggregate_and_clear(LogBuffer *logs);

typedef struct Threat
{
    int type;
    bool engaged;
    float x, y, z;
    float vx, vy, vz;
    float az, el;
    float fov;
    float track_rate;
    float guidance_gain;
    float engagement_radius;
    float lethal_radius;
    float acceleration;
    float max_velocity;
    float lifetime;
} Threat;

typedef struct Laser
{
    int type;
    int engaging_threat;
    float az, el;
    float track_rate;
    float fov;
    float guidance_reduction;
    float maximum_range;
} Laser;

typedef struct AirSim
{
    // Simulation parameters
    float initial_distance;
    float aircraft_speed;
    float threat_acceleration;
    float threat_max_velocity;
    float engagement_radius;

    // Core simulation state
    Threat threats[MAX_THREATS];
    Laser lasers[MAX_LASERS];
    float time;
    int ticks;
    char terminal;

    // Aircraft state
    float aircraft_x, aircraft_y, aircraft_z;
    float aircraft_vx, aircraft_vy, aircraft_vz;

    // Environment parameters
    float dt;
    float max_time;
    int max_steps;

    // Environment interface
    float *observations;
    int *actions;
    float *rewards;
    unsigned char *terminals;

    LogBuffer *log_buffer;
    Log log;
} AirSim;

typedef struct SimConfig
{
    // Global simulation parameters
    float initial_distance;
    float aircraft_speed;
    float threat_acceleration;
    float threat_max_velocity;
    float engagement_radius;
    float dt;
    float max_time;
    int max_steps;

    // Type definitions
    struct
    {
        float track_rate;
        float fov;
        float guidance_reduction;
        float maximum_range;
    } laser_types[10]; // Support up to 10 types

    struct
    {
        float fov;
        float track_rate;
        float guidance_gain;
        float engagement_radius;
        float lethal_radius;
        float acceleration;
        float max_velocity;
        float lifetime;
    } threat_types[10]; // Support up to 10 types

    // Entity definitions
    struct
    {
        int type; // 0 = inactive, 1+ = type index
    } lasers[MAX_LASERS];

    struct
    {
        int type; // 0 = inactive, 1+ = type index
    } threats[MAX_THREATS];
} SimConfig;

// yaml config loading

// Helper function to safely get float from YAML node
float get_yaml_float(yaml_document_t *document, yaml_node_t *node, const char *key, float default_value)
{
    if (!node)
        return default_value;

    for (int i = 0; i < node->data.mapping.pairs.top - node->data.mapping.pairs.start; i++)
    {
        yaml_node_pair_t *pair = node->data.mapping.pairs.start + i;
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);
        if (strcmp((char *)key_node->data.scalar.value, key) == 0)
        {
            yaml_node_t *value_node = yaml_document_get_node(document, pair->value);
            return atof((char *)value_node->data.scalar.value);
        }
    }
    return default_value;
}

// Helper function to safely get int from YAML node
int get_yaml_int(yaml_document_t *document, yaml_node_t *node, const char *key, int default_value)
{
    if (!node)
        return default_value;

    for (int i = 0; i < node->data.mapping.pairs.top - node->data.mapping.pairs.start; i++)
    {
        yaml_node_pair_t *pair = node->data.mapping.pairs.start + i;
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);
        if (strcmp((char *)key_node->data.scalar.value, key) == 0)
        {
            yaml_node_t *value_node = yaml_document_get_node(document, pair->value);
            return atoi((char *)value_node->data.scalar.value);
        }
    }
    return default_value;
}

// Helper function to get a node by name from a mapping node
yaml_node_t *get_yaml_node(yaml_document_t *document, yaml_node_t *node, const char *key)
{
    if (!node)
        return NULL;

    for (int i = 0; i < node->data.mapping.pairs.top - node->data.mapping.pairs.start; i++)
    {
        yaml_node_pair_t *pair = node->data.mapping.pairs.start + i;
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);
        if (strcmp((char *)key_node->data.scalar.value, key) == 0)
        {
            return yaml_document_get_node(document, pair->value);
        }
    }
    return NULL;
}

SimConfig *load_config(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        fprintf(stderr, "Failed to open config file: %s\n", filename);
        return NULL;
    }

    yaml_parser_t parser;
    yaml_document_t document;
    SimConfig *config = NULL;

    if (!yaml_parser_initialize(&parser))
    {
        fclose(file);
        return NULL;
    }

    yaml_parser_set_input_file(&parser, file);

    if (!yaml_parser_load(&parser, &document))
    {
        yaml_parser_delete(&parser);
        fclose(file);
        return NULL;
    }

    config = (SimConfig *)calloc(1, sizeof(SimConfig));
    if (!config)
    {
        yaml_document_delete(&document);
        yaml_parser_delete(&parser);
        fclose(file);
        return NULL;
    }

    // Get root node
    yaml_node_t *root = yaml_document_get_root_node(&document);
    if (!root)
        goto cleanup;

    // Load simulation settings
    yaml_node_t *sim_node = get_yaml_node(&document, root, "simulation");
    if (sim_node)
    {
        config->initial_distance = get_yaml_float(&document, sim_node, "initial_distance", 1000.0f);
        config->aircraft_speed = get_yaml_float(&document, sim_node, "aircraft_speed", 100.0f);
        config->threat_acceleration = get_yaml_float(&document, sim_node, "threat_acceleration", 400.0f);
        config->threat_max_velocity = get_yaml_float(&document, sim_node, "threat_max_velocity", 1000.0f);
        config->engagement_radius = get_yaml_float(&document, sim_node, "engagement_radius", 2500.0f);
        config->dt = get_yaml_float(&document, sim_node, "dt", 0.016f);
        config->max_time = get_yaml_float(&document, sim_node, "max_time", 60.0f);
        config->max_steps = get_yaml_int(&document, sim_node, "max_steps", 6000);
    }

    // Load laser types
    yaml_node_t *laser_types = get_yaml_node(&document, root, "laser_types");
    if (laser_types)
    {
        for (int i = 0; i < laser_types->data.mapping.pairs.top - laser_types->data.mapping.pairs.start; i++)
        {
            yaml_node_pair_t *pair = laser_types->data.mapping.pairs.start + i;
            yaml_node_t *key_node = yaml_document_get_node(&document, pair->key);
            yaml_node_t *value_node = yaml_document_get_node(&document, pair->value);

            int type_idx = atoi((char *)key_node->data.scalar.value);
            if (type_idx > 0 && type_idx < 10)
            {
                config->laser_types[type_idx].track_rate = get_yaml_float(&document, value_node, "track_rate", 60.0f);
                config->laser_types[type_idx].fov = get_yaml_float(&document, value_node, "fov", 5.0f);
                config->laser_types[type_idx].guidance_reduction = get_yaml_float(&document, value_node, "guidance_reduction", 1.0f);
                config->laser_types[type_idx].maximum_range = get_yaml_float(&document, value_node, "maximum_range", 4000.0f);
            }
        }
    }

    // Load threat types
    yaml_node_t *threat_types = get_yaml_node(&document, root, "threat_types");
    if (threat_types)
    {
        for (int i = 0; i < threat_types->data.mapping.pairs.top - threat_types->data.mapping.pairs.start; i++)
        {
            yaml_node_pair_t *pair = threat_types->data.mapping.pairs.start + i;
            yaml_node_t *key_node = yaml_document_get_node(&document, pair->key);
            yaml_node_t *value_node = yaml_document_get_node(&document, pair->value);

            int type_idx = atoi((char *)key_node->data.scalar.value);
            if (type_idx > 0 && type_idx < 10)
            {
                config->threat_types[type_idx].fov = get_yaml_float(&document, value_node, "fov", 30.0f);
                config->threat_types[type_idx].track_rate = get_yaml_float(&document, value_node, "track_rate", 100.0f);
                config->threat_types[type_idx].guidance_gain = get_yaml_float(&document, value_node, "guidance_gain", 1.0f);
                config->threat_types[type_idx].engagement_radius = get_yaml_float(&document, value_node, "engagement_radius", 2500.0f);
                config->threat_types[type_idx].lethal_radius = get_yaml_float(&document, value_node, "lethal_radius", 30.0f);
                config->threat_types[type_idx].acceleration = get_yaml_float(&document, value_node, "acceleration", 400.0f);
                config->threat_types[type_idx].max_velocity = get_yaml_float(&document, value_node, "max_velocity", 1000.0f);
                config->threat_types[type_idx].lifetime = get_yaml_float(&document, value_node, "lifetime", 17.0f);
            }
        }
    }

    // Load entity configurations
    yaml_node_t *entities = get_yaml_node(&document, root, "entities");
    if (entities)
    {
        // Load laser configurations
        yaml_node_t *lasers = get_yaml_node(&document, entities, "lasers");
        if (lasers)
        {
            for (int i = 0; i < lasers->data.sequence.items.top - lasers->data.sequence.items.start; i++)
            {
                if (i >= MAX_LASERS)
                    break;
                yaml_node_t *laser = yaml_document_get_node(&document, lasers->data.sequence.items.start[i]);
                config->lasers[i].type = get_yaml_int(&document, laser, "type", 0);
            }
        }

        // Load threat configurations
        yaml_node_t *threats = get_yaml_node(&document, entities, "threats");
        if (threats)
        {
            for (int i = 0; i < threats->data.sequence.items.top - threats->data.sequence.items.start; i++)
            {
                if (i >= MAX_THREATS)
                    break;
                yaml_node_t *threat = yaml_document_get_node(&document, threats->data.sequence.items.start[i]);
                config->threats[i].type = get_yaml_int(&document, threat, "type", 0);
            }
        }
    }

cleanup:
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    fclose(file);
    return config;
}

void apply_config(AirSim *sim, SimConfig *config)
{
    // Apply simulation settings
    sim->initial_distance = config->initial_distance;
    sim->aircraft_speed = config->aircraft_speed;
    sim->threat_acceleration = config->threat_acceleration;
    sim->threat_max_velocity = config->threat_max_velocity;
    sim->engagement_radius = config->engagement_radius;
    sim->dt = config->dt;
    sim->max_time = config->max_time;
    sim->max_steps = config->max_steps;

    // Apply laser configurations
    for (int i = 0; i < MAX_LASERS; i++)
    {
        int type = config->lasers[i].type;
        if (type > 0)
        {
            sim->lasers[i].type = type;
            sim->lasers[i].track_rate = config->laser_types[type].track_rate;
            sim->lasers[i].fov = config->laser_types[type].fov;
            sim->lasers[i].guidance_reduction = config->laser_types[type].guidance_reduction;
            sim->lasers[i].maximum_range = config->laser_types[type].maximum_range;
        }
        else
        {
            sim->lasers[i].type = 0;
        }
    }

    // Apply threat configurations
    for (int i = 0; i < MAX_THREATS; i++)
    {
        int type = config->threats[i].type;
        if (type > 0)
        {
            sim->threats[i].type = type;
            sim->threats[i].fov = config->threat_types[type].fov;
            sim->threats[i].track_rate = config->threat_types[type].track_rate;
            sim->threats[i].guidance_gain = config->threat_types[type].guidance_gain;
            sim->threats[i].engagement_radius = config->threat_types[type].engagement_radius;
            sim->threats[i].lethal_radius = config->threat_types[type].lethal_radius;
            sim->threats[i].acceleration = config->threat_types[type].acceleration;
            sim->threats[i].max_velocity = config->threat_types[type].max_velocity;
            sim->threats[i].lifetime = config->threat_types[type].lifetime;
        }
        else
        {
            sim->threats[i].type = 0;
        }
    }
}

void free_config(SimConfig *config)
{
    if (config)
    {
        free(config);
    }
}

// Helper functions
float compute_distance(float x1, float y1, float z1, float x2, float y2, float z2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

void compute_angles_to_target(float x1, float y1, float z1,
                              float x2, float y2, float z2,
                              float *az, float *el)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;

    *az = atan2f(dy, dx) * RAD2DEG_SAFE;
    float ground_distance = sqrtf(dx * dx + dy * dy);
    *el = atan2f(dz, ground_distance) * RAD2DEG_SAFE;
}

// Add this helper function for predicted path calculation
void predict_pronav_point(
    float dt,
    float aircraft_x, float aircraft_y, float aircraft_z,
    float aircraft_vx, float aircraft_vy, float aircraft_vz,
    float *threat_x, float *threat_y, float *threat_z,
    float *threat_vx, float *threat_vy, float *threat_vz,
    float guidance_gain, float max_accel, float max_velocity,
    bool debug,
    // Add new parameters
    int step,
    int threat_id,
    bool is_engaged,
    bool being_targeted)
{
    // Calculate relative position (from threat to target)
    float dx = aircraft_x - *threat_x;
    float dy = aircraft_y - *threat_y;
    float dz = aircraft_z - *threat_z;

    float r = sqrtf(dx * dx + dy * dy + dz * dz);
    if (r < 0.1f)
        return;

    // Unit LOS vector from threat to target
    float rx = dx / r;
    float ry = dy / r;
    float rz = dz / r;

    // Relative velocity
    float dvx = aircraft_vx - *threat_vx;
    float dvy = aircraft_vy - *threat_vy;
    float dvz = aircraft_vz - *threat_vz;

    // Closing velocity (positive means closing)
    float vc = (dvx * rx + dvy * ry + dvz * rz);

    // Calculate omega vector directly using cross product
    float omega_x = (dy * dvz - dz * dvy) / (r * r);
    float omega_y = (dz * dvx - dx * dvz) / (r * r);
    float omega_z = (dx * dvy - dy * dvx) / (r * r);

    float omega_mag = sqrtf(omega_x * omega_x + omega_y * omega_y + omega_z * omega_z);

    // Pure PN law: n = N * Vc * omega
    float N = 3.0f;
    float scale = N * vc * guidance_gain;

    // Compute acceleration perpendicular to velocity
    float v_mag = sqrtf((*threat_vx) * (*threat_vx) + (*threat_vy) * (*threat_vy) + (*threat_vz) * (*threat_vz));
    if (v_mag < 0.1f)
        v_mag = 0.1f;

    // Cross omega with velocity direction to get lateral acceleration
    float ax = scale * ((*threat_vy * omega_z - *threat_vz * omega_y) / v_mag);
    float ay = scale * ((*threat_vz * omega_x - *threat_vx * omega_z) / v_mag);
    float az = scale * ((*threat_vx * omega_y - *threat_vy * omega_x) / v_mag);

    // Limit total acceleration
    float a_mag = sqrtf(ax * ax + ay * ay + az * az);
    if (a_mag > max_accel)
    {
        float scale = max_accel / a_mag;
        ax *= scale;
        ay *= scale;
        az *= scale;
    }

#ifdef DEBUG_PRONAV
    if (debug)
    {
        printf("\nPRONAV [Step %d] Threat %d (%s, %s):\n",
               step, threat_id,
               is_engaged ? "ENGAGED" : "NOT ENGAGED",
               being_targeted ? "TARGETED" : "NOT TARGETED");
        printf("Range: %.1fm, Closing Vel: %.1fm/s\n", r, vc);
        printf("LOS Rate: %.2f deg/s\n", omega_mag * RAD2DEG_SAFE);
        printf("Velocity: [%.1f, %.1f, %.1f] m/s (mag: %.1f)\n",
               *threat_vx, *threat_vy, *threat_vz, v_mag);
        printf("Accel: [%.1f, %.1f, %.1f] m/s² (mag: %.1f)\n",
               ax, ay, az, a_mag);
    }
#endif

    // Update velocities
    *threat_vx += ax * dt;
    *threat_vy += ay * dt;
    *threat_vz += az * dt;

    // Limit velocity magnitude
    v_mag = sqrtf((*threat_vx) * (*threat_vx) + (*threat_vy) * (*threat_vy) + (*threat_vz) * (*threat_vz));
    if (v_mag > max_velocity)
    {
        float scale = max_velocity / v_mag;
        *threat_vx *= scale;
        *threat_vy *= scale;
        *threat_vz *= scale;
    }

    // Update positions
    *threat_x += *threat_vx * dt;
    *threat_y += *threat_vy * dt;
    *threat_z += *threat_vz * dt;
}

// Core simulation functions
void step_aircraft(AirSim *sim)
{
    // Update aircraft position based on velocity
    sim->aircraft_x += sim->aircraft_vx * sim->dt;
    sim->aircraft_y += sim->aircraft_vy * sim->dt;
    sim->aircraft_z += sim->aircraft_vz * sim->dt;

    // Keep moving at constant velocity
    sim->aircraft_vx = 100.0f; // Maintain forward speed
}

void engage_threats(AirSim *sim)
{
#ifdef DEBUG_PRINT
    printf("Debug: Engaging threats\n");
#endif
    for (int i = 0; i < MAX_THREATS; i++)
    {
        if (sim->threats[i].type > 0)
        {
#ifdef DEBUG_PRINT
            printf("Debug: Threat %d at (%f, %f, %f), engaged %d\n",
                   i, sim->threats[i].x, sim->threats[i].y, sim->threats[i].z, sim->threats[i].engaged);
#endif
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z);
            if (dist <= sim->threats[i].engagement_radius)
            {
                sim->threats[i].engaged = true;
            }
        }
    }
}

void step_threats(AirSim *sim)
{
    for (int i = 0; i < MAX_THREATS; i++)
    {
        if (sim->threats[i].type > 0 && sim->threats[i].engaged)
        {
            // Check if any laser is targeting this threat
            bool being_targeted = false;
            for (int j = 0; j < MAX_LASERS; j++)
            {
                if (sim->lasers[j].engaging_threat == i)
                {
                    being_targeted = true;
                    break;
                }
            }

            // Use predict_pronav_point for actual simulation step
            predict_pronav_point(
                sim->dt,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                sim->aircraft_vx, sim->aircraft_vy, sim->aircraft_vz,
                &sim->threats[i].x, &sim->threats[i].y, &sim->threats[i].z,
                &sim->threats[i].vx, &sim->threats[i].vy, &sim->threats[i].vz,
                sim->threats[i].guidance_gain,
                sim->threats[i].acceleration,
                sim->threats[i].max_velocity,
                true,                    // debug
                sim->ticks,              // step
                i,                       // threat_id
                sim->threats[i].engaged, // is_engaged
                being_targeted           // being_targeted
            );

            // Update angles for visualization
            compute_angles_to_target(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                &sim->threats[i].az, &sim->threats[i].el);

            // Check for ground collision
            if (sim->threats[i].z <= 0.0f)
            {
                sim->threats[i].type = 0;
                sim->threats[i].lifetime = 0.0f;
                continue;
            }

            // Update lifetime
            sim->threats[i].lifetime -= sim->dt;
            if (sim->threats[i].lifetime <= 0)
            {
                sim->threats[i].type = 0;
            }
        }
    }
}

void slew_lasers(AirSim *sim)
{
    for (int i = 0; i < MAX_LASERS; i++)
    {
        if (sim->lasers[i].type > 0 && sim->lasers[i].engaging_threat >= 0)
        {
            int threat_idx = sim->lasers[i].engaging_threat;
#ifdef DEBUG_PRINT
            printf("[Step %d] SlewLasers: Laser %d attempting to engage threat %d (type=%d)\n", sim->ticks,
                   i, threat_idx, sim->threats[threat_idx].type);
#endif
            if (sim->threats[threat_idx].type > 0)
            {
                float target_az, target_el;
                compute_angles_to_target(
                    sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                    sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z,
                    &target_az, &target_el);
#ifdef DEBUG_PRINT
                printf("[Step %d] SlewLasers: Laser %d -> Threat %d, Current(az=%.1f, el=%.1f) Target(az=%.1f, el=%.1f)\n", sim->ticks,
                       i, threat_idx, sim->lasers[i].az, sim->lasers[i].el, target_az, target_el);
#endif
                // Calculate angle differences
                float az_diff = fmodf(target_az - sim->lasers[i].az + 540.0f, 360.0f) - 180.0f;
                float el_diff = target_el - sim->lasers[i].el;

                // Apply slew limits
                float max_angle = sim->lasers[i].track_rate * sim->dt;
                if (az_diff > max_angle)
                    az_diff = max_angle;
                if (az_diff < -max_angle)
                    az_diff = -max_angle;
                if (el_diff > max_angle)
                    el_diff = max_angle;
                if (el_diff < -max_angle)
                    el_diff = -max_angle;

                // Update angles
                sim->lasers[i].az = fmodf(sim->lasers[i].az + az_diff + 360.0f, 360.0f);
                sim->lasers[i].el = fminf(90.0f, fmaxf(-90.0f, sim->lasers[i].el + el_diff));
            }
            else
            {
// Don't reset engagement - let manual control handle this
#ifdef DEBUG_PRINT
                printf("[Step %d] SlewLasers: Laser %d target threat %d is inactive\n", sim->ticks,
                       i, threat_idx);
#endif
            }
        }
    }
}

void apply_countermeasures(AirSim *sim)
{
    for (int i = 0; i < MAX_LASERS; i++)
    {
        if (sim->lasers[i].type > 0 && sim->lasers[i].engaging_threat >= 0)
        {
            int threat_idx = sim->lasers[i].engaging_threat;
            if (sim->threats[threat_idx].type > 0)
            {
                float target_az, target_el;
                compute_angles_to_target(
                    sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                    sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z,
                    &target_az, &target_el);

                // Check if threat is in FOV
                float az_diff = fabsf(fmodf(target_az - sim->lasers[i].az + 540.0f, 360.0f) - 180.0f);
                float el_diff = fabsf(target_el - sim->lasers[i].el);

                if (az_diff <= sim->lasers[i].fov && el_diff <= sim->lasers[i].fov)
                {
                    float dist = compute_distance(
                        sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                        sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z);

                    if (dist <= sim->lasers[i].maximum_range)
                    {
                        // Reduce guidance gain
                        sim->threats[threat_idx].guidance_gain = fmaxf(0.0f,
                                                                       sim->threats[threat_idx].guidance_gain -
                                                                           sim->lasers[i].guidance_reduction * sim->dt);
                    }
                }
            }
        }
    }
}

char check_terminal(AirSim *sim)
{
    // Check if any threat is within lethal radius
    for (int i = 0; i < MAX_THREATS; i++)
    {
        if (sim->threats[i].type > 0 && sim->threats[i].engaged)
        {
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z);
            if (dist <= sim->threats[i].lethal_radius)
            {
                return 1;
            }
        }
    }

    // Check if all active threats have expired
    bool all_threats_expired = false;
    bool had_threats = false;
    for (int i = 0; i < MAX_THREATS; i++)
    {
        if (sim->threats[i].type > 0)
        {
            had_threats = true;
            if (sim->threats[i].lifetime > 0)
            {
                all_threats_expired = false;
                break;
            }
        }
    }
    if (had_threats && all_threats_expired)
    {
        return 1;
    }

    // Check time limits
    if (sim->time >= sim->max_time || sim->ticks >= sim->max_steps)
    {
        return 1;
    }

    return 0;
}

void compute_rewards(AirSim *sim)
{
    float reward = 0.0f;

    // Reward for surviving until threats expire
    for (int i = 0; i < MAX_THREATS; i++)
    {
        if (sim->threats[i].type == 0 && sim->threats[i].lifetime <= 0)
        {
            reward += 10.0f;
        }
    }

    // Penalty for terminal state (threat within lethal radius)
    if (sim->terminal)
    {
        reward -= 100.0f;
    }

    sim->rewards[0] = reward;
}
void process_actions(AirSim *sim)
{
    for (int i = 0; i < MAX_LASERS; i++)
    {
        if (sim->lasers[i].engaging_threat == -1)
        {
            if (sim->actions[i] >= 0 && sim->actions[i] < MAX_THREATS &&
                sim->threats[sim->actions[i]].type > 0)
            {
                sim->lasers[i].engaging_threat = sim->actions[i];
            }
        }
    }
}

void update_observations(AirSim *sim)
{
    int obs_idx = 0;
    sim->observations[obs_idx++] = sim->aircraft_x;
    sim->observations[obs_idx++] = sim->aircraft_y;
    sim->observations[obs_idx++] = sim->aircraft_z;
    sim->observations[obs_idx++] = sim->aircraft_vx;
    sim->observations[obs_idx++] = sim->aircraft_vy;
    sim->observations[obs_idx++] = sim->aircraft_vz;

    for (int i = 0; i < MAX_THREATS; i++)
    {
        sim->observations[obs_idx++] = (float)sim->threats[i].type;
        sim->observations[obs_idx++] = sim->threats[i].x;
        sim->observations[obs_idx++] = sim->threats[i].y;
        sim->observations[obs_idx++] = sim->threats[i].z;
        sim->observations[obs_idx++] = sim->threats[i].vx;
        sim->observations[obs_idx++] = sim->threats[i].vy;
        sim->observations[obs_idx++] = sim->threats[i].vz;
        sim->observations[obs_idx++] = sim->threats[i].guidance_gain; // Replace HP with guidance_gain
    }

    for (int i = 0; i < MAX_LASERS; i++)
    {
        sim->observations[obs_idx++] = (float)sim->lasers[i].type;
        sim->observations[obs_idx++] = (float)sim->lasers[i].engaging_threat;
        sim->observations[obs_idx++] = sim->lasers[i].az;
        sim->observations[obs_idx++] = sim->lasers[i].el;
    }
}
// Main simulation step
void step(AirSim *sim)
{
#ifdef DEBUG_PRINT
    printf("Debug Step: Using observations buffer at %p\n", sim->observations);
    printf("Debug: Starting tick %d\n", sim->ticks);
#endif
    process_actions(sim);

#ifdef DEBUG_PRINT
    printf("Debug: Updating aircraft position from (%f, %f, %f)\n",
           sim->aircraft_x, sim->aircraft_y, sim->aircraft_z);
#endif
    step_aircraft(sim);
#ifdef DEBUG_PRINT
    printf("Debug: New aircraft position (%f, %f, %f)\n",
           sim->aircraft_x, sim->aircraft_y, sim->aircraft_z);
#endif
    engage_threats(sim);
#ifdef DEBUG_PRINT
    printf("Debug: After Engaging threats\n");
#endif
    step_threats(sim);
#ifdef DEBUG_PRINT
    printf("Debug: After stepping threats\n");
#endif
    slew_lasers(sim);
#ifdef DEBUG_PRINT
    printf("Debug: After slewing lasers\n");
#endif
    apply_countermeasures(sim);
#ifdef DEBUG_PRINT
    printf("Debug: After applying damage\n");
#endif

    sim->terminal = check_terminal(sim);
#ifdef DEBUG_TERMINAL
    if (sim->terminal)
    {
        printf("Terminal state reached.\n");
    }
#endif
    sim->terminals[0] = sim->terminal;

    compute_rewards(sim);
    update_observations(sim);

    sim->time += sim->dt;
    sim->ticks += 1;
}

// Reset simulation
void reset(AirSim *sim)
{
    sim->time = 0.0f;
    sim->ticks = 0;
    sim->terminal = 0;
    sim->terminals[0] = 0;

    // Reset aircraft with configured velocity
    sim->aircraft_x = 0.0f;
    sim->aircraft_y = 0.0f;
    sim->aircraft_z = 1000.0f;
    sim->aircraft_vx = sim->aircraft_speed;
    sim->aircraft_vy = 0.0f;
    sim->aircraft_vz = 0.0f;

    // Reset threats
    for (int i = 0; i < MAX_THREATS; i++)
    {
        sim->threats[i].type = (i < 3) ? 1 : 0; // Start with 3 active threats
        sim->threats[i].engaged = false;

        // Position threats in front of aircraft in a spread pattern
        float angle = ((float)i - 1.5f) * PI / 6.0f;                      // Spread threats across 60 degrees
        float rand_z = ((float)rand() / (float)RAND_MAX - 0.5f) * 200.0f; // Limit Z variation to ±100m

        sim->threats[i].x = sim->aircraft_x + sim->initial_distance * cosf(angle);
        sim->threats[i].y = sim->aircraft_y + sim->initial_distance * sinf(angle);
        sim->threats[i].z = 300.0f + rand_z; // have threats start at ~300m altitude

        sim->threats[i].vx = 0.0f;
        sim->threats[i].vy = 0.0f;
        sim->threats[i].vz = 0.0f;
        sim->threats[i].engagement_radius = sim->engagement_radius;
        sim->threats[i].lethal_radius = 30.0f;
        sim->threats[i].acceleration = sim->threat_acceleration;
        sim->threats[i].max_velocity = sim->threat_max_velocity;
        sim->threats[i].lifetime = 17.0f;
        sim->threats[i].guidance_gain = 1.0f;
        sim->threats[i].track_rate = 100.0f;
        sim->threats[i].fov = 30.0f;

        // Initialize pointing angles toward aircraft
        compute_angles_to_target(
            sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
            sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
            &sim->threats[i].az, &sim->threats[i].el);

        // Give threats initial velocity components toward aircraft
        // This helps overcome initial inertia and gets them moving toward target
        float dx = sim->aircraft_x - sim->threats[i].x;
        float dy = sim->aircraft_y - sim->threats[i].y;
        float dz = sim->aircraft_z - sim->threats[i].z;
        float r = sqrtf(dx * dx + dy * dy + dz * dz);

        // Initialize with 75% of max velocity toward target
        float init_speed = sim->threat_max_velocity * 0.75f;
        sim->threats[i].vx = (dx / r) * init_speed;
        sim->threats[i].vy = (dy / r) * init_speed;
        sim->threats[i].vz = (dz / r) * init_speed;
    }

    // Reset lasers
    for (int i = 0; i < MAX_LASERS; i++)
    {
        sim->lasers[i].type = 0;
        // if (i < 2) sim->lasers[i].type = 1;     // Start with 2 active lasers
        sim->lasers[i].engaging_threat = -1;
        sim->lasers[i].az = 0.0f;
        sim->lasers[i].el = 0.0f;
        sim->lasers[i].track_rate = 60.0f;
        sim->lasers[i].fov = 5.0f;
        sim->lasers[i].guidance_reduction = 1.0f; // Reduces guidance by 100% per second
        sim->lasers[i].maximum_range = 4000.0f;
    }

    update_observations(sim);
    sim->rewards[0] = 0.0f;
    sim->terminals[0] = false;
}

void init(AirSim *sim)
{
    reset(sim);
}

// Allocate simulation buffers
void allocate(AirSim *sim)
{
    // Allocate memory before initialization
    int obs_size = 6 + 8 * MAX_THREATS + 4 * MAX_LASERS; // Fix observation size calculation
    sim->observations = (float *)calloc(obs_size, sizeof(float));
    sim->actions = (int *)calloc(MAX_LASERS, sizeof(int));
    sim->rewards = (float *)calloc(1, sizeof(float));
    sim->terminals = (unsigned char *)calloc(1, sizeof(unsigned char));
    sim->log_buffer = allocate_logbuffer(LOG_BUFFER_SIZE);

    // Verify allocations
    if (!sim->observations || !sim->actions || !sim->rewards ||
        !sim->terminals || !sim->log_buffer)
    {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(1);
    }

    // Initialize after memory allocation
    init(sim);
}

// Free simulation buffers
void free_allocated(AirSim *sim)
{
    if (!sim)
        return;
    if (sim->observations)
        free(sim->observations);
    if (sim->actions)
        free(sim->actions);
    if (sim->rewards)
        free(sim->rewards);
    if (sim->terminals)
        free(sim->terminals);
    if (sim->log_buffer)
        free_logbuffer(sim->log_buffer);

    sim->observations = NULL;
    sim->actions = NULL;
    sim->rewards = NULL;
    sim->terminals = NULL;
    sim->log_buffer = NULL;
}

#endif
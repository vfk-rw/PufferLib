#ifndef AIRSIM_H
#define AIRSIM_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "raylib.h"
#include "yaml.h"
#include "log.h" // Include log.h first

#define PI 3.14159265358979323846f
#define MAX_SEEKERS 10
#define MAX_LASERS 5
#define LOG_BUFFER_SIZE 1024

#undef DEBUG_PRINT
// Forward declare log functions
LogBuffer *allocate_logbuffer(int size);
void free_logbuffer(LogBuffer *buffer);
void add_log(LogBuffer *logs, Log *log);
Log aggregate_and_clear(LogBuffer *logs);

// --- Structures for Simulation Entities ---

// Aircraft structure
typedef struct Aircraft
{
    float x, y, z;
    float vx, vy, vz;
} Aircraft;

// Seeker structure: these missiles use PN guidance and target only the aircraft.
typedef struct Seeker
{
    // Configuration parameters (loaded from YAML)
    int type;           // Categorical, one-hot encoding in observations later.
    float fov;          // Field of view in degrees.
    float track_rate;   // Maximum turn rate (degrees per second).
    float acceleration; // Maximum acceleration.
    float max_velocity;
    float lifetime;            // Total active time in seconds.
    float navigation_constant; // e.g., N value for PN guidance.

    // Dynamic state
    float x, y, z;
    float vx, vy, vz;
    float current_heading; // In degrees, computed from velocity vector.
    bool active;           // If false, seeker is inactive.

    float elapsed_time; // How long it has been active.
} Seeker;

// Laser structure: defensive countermeasure.
typedef struct Laser
{
    // Configuration parameters (loaded from YAML)
    int type;                 // Categorical, one-hot encoding in observation.
    float fov;                // Field of view in degrees.
    float track_rate;         // Slewing speed (degrees per second).
    float guidance_reduction; // How much error is induced per second.
    float maximum_range;      // Maximum effective range.

    // Dynamic state
    int engaging_seeker; // Index of seeker being targeted; -1 if none.
    float az;            // Current azimuth angle.
    float el;            // Current elevation angle.
} Laser;

// Simulation configuration from YAML.
typedef struct SimConfig
{
    // General simulation parameters
    float dt;       // Timestep in seconds.
    float max_time; // Maximum simulation time.
    int max_steps;  // Maximum simulation steps.

    // Aircraft parameters
    float aircraft_x;
    float aircraft_y;
    float aircraft_z;
    float aircraft_vx;
    float aircraft_vy;
    float aircraft_vz;

    // Seeker type parameters: indexed from 1 up to MAX_SEEKERS.
    // We support up to 10 types.
    struct
    {
        float fov;
        float track_rate;
        float acceleration;
        float max_velocity;
        float lifetime;
        float navigation_constant;
    } seeker_types[11];

    // Laser type parameters: indexed from 1 up to MAX_LASERS.
    struct
    {
        float track_rate;
        float fov;
        float guidance_reduction;
        float maximum_range;
    } laser_types[11];

    // Entities: define which seekers and lasers are active and their types.
    int seekers[MAX_SEEKERS]; // For each seeker slot, a type > 0 means active.
    int lasers[MAX_LASERS];   // For each laser slot, type > 0 means active.

    // Initial conditions for aircraft, seekers can also be set here if desired.
} SimConfig;

// Main simulation structure.
typedef struct AirSim
{
    // Simulation time and step
    float time;
    int ticks;
    bool terminal;

    // Entities
    Aircraft aircraft;
    Seeker seekers[MAX_SEEKERS];
    Laser lasers[MAX_LASERS];

    // Environment parameters
    float dt;
    float max_time;
    int max_steps;

    // Observations, actions, rewards, terminals for pufferlib interface.
    // Observations: flat float array. We'll compute:
    // - 6 for aircraft state (position, velocity).
    // - For each seeker (MAX_SEEKERS): 1 (one-hot encoded type: dimension = number of seeker types) + 3 relative pos + 3 relative vel.
    //   For simplicity, assume 10 possible types, so 10 + 6 per seeker.
    // - For each laser (MAX_LASERS): one-hot encoded type (assume 10), plus 1 for engaging_seeker, plus 2 for angles.
    float *observations;
    int *actions; // Array of length MAX_LASERS; each value: target seeker index (-1 means none).
    float *rewards;
    unsigned char *terminals;

    // Log buffer for simulation state logging.
    LogBuffer *log_buffer;
    Log log;

    // YAML configuration used to initialize the simulation.
    SimConfig config;
} AirSim;

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

// --- YAML Config Loading Functions ---
/**
 * Loads a YAML configuration file and populates a SimConfig structure.
 * Returns a pointer to a newly allocated SimConfig, or NULL on failure.
 */
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

    // Get root node.
    yaml_node_t *root = yaml_document_get_root_node(&document);
    if (!root)
        goto cleanup;

    // Load simulation settings.
    yaml_node_t *sim_node = get_yaml_node(&document, root, "simulation");
    if (sim_node)
    {
        config->dt = get_yaml_float(&document, sim_node, "dt", 0.001f);
        config->max_time = get_yaml_float(&document, sim_node, "max_time", 60.0f);
        config->max_steps = get_yaml_int(&document, sim_node, "max_steps", 60000);
        // Print simulation settings
        printf("Loaded simulation settings:\n");
        printf("  dt: %.3f\n", config->dt);
        printf("  max_time: %.1f\n", config->max_time);
        printf("  max_steps: %d\n", config->max_steps);
        // Load aircraft settings.
        yaml_node_t *ac_node = get_yaml_node(&document, sim_node, "aircraft");
        if (ac_node)
        {
            yaml_node_t *pos_node = get_yaml_node(&document, ac_node, "position");
            if (pos_node && pos_node->type == YAML_SEQUENCE_NODE &&
                pos_node->data.sequence.items.top - pos_node->data.sequence.items.start >= 3)
            {
                config->aircraft_x = atof((char *)yaml_document_get_node(&document, pos_node->data.sequence.items.start[0])->data.scalar.value);
                config->aircraft_y = atof((char *)yaml_document_get_node(&document, pos_node->data.sequence.items.start[1])->data.scalar.value);
                config->aircraft_z = atof((char *)yaml_document_get_node(&document, pos_node->data.sequence.items.start[2])->data.scalar.value);
            }
            yaml_node_t *vel_node = get_yaml_node(&document, ac_node, "velocity");
            if (vel_node && vel_node->type == YAML_SEQUENCE_NODE &&
                vel_node->data.sequence.items.top - vel_node->data.sequence.items.start >= 3)
            {
                config->aircraft_vx = atof((char *)yaml_document_get_node(&document, vel_node->data.sequence.items.start[0])->data.scalar.value);
                config->aircraft_vy = atof((char *)yaml_document_get_node(&document, vel_node->data.sequence.items.start[1])->data.scalar.value);
                config->aircraft_vz = atof((char *)yaml_document_get_node(&document, vel_node->data.sequence.items.start[2])->data.scalar.value);
            }
        }
    }

    // Add debug prints
    printf("Loading config from %s\n", filename);

    // Load seeker types
    yaml_node_t *seeker_types = get_yaml_node(&document, root, "seeker_types");
    if (seeker_types)
    {
        printf("Found seeker_types section\n");
        for (int i = 0; i < seeker_types->data.mapping.pairs.top - seeker_types->data.mapping.pairs.start; i++)
        {
            yaml_node_pair_t *pair = seeker_types->data.mapping.pairs.start + i;
            yaml_node_t *key_node = yaml_document_get_node(&document, pair->key);
            yaml_node_t *value_node = yaml_document_get_node(&document, pair->value);

            int type_idx = atoi((char *)key_node->data.scalar.value);
            printf("Loading seeker type %d\n", type_idx);

            if (type_idx > 0 && type_idx < 11)
            {
                config->seeker_types[type_idx].fov = get_yaml_float(&document, value_node, "fov", 30.0f);
                config->seeker_types[type_idx].track_rate = get_yaml_float(&document, value_node, "track_rate", 100.0f);
                config->seeker_types[type_idx].acceleration = get_yaml_float(&document, value_node, "acceleration", 400.0f);
                config->seeker_types[type_idx].max_velocity = get_yaml_float(&document, value_node, "max_velocity", 1000.0f);
                config->seeker_types[type_idx].lifetime = get_yaml_float(&document, value_node, "lifetime", 17.0f);
                config->seeker_types[type_idx].navigation_constant = get_yaml_float(&document, value_node, "navigation_constant", 3.0f);
            }
        }
    }

    // Load laser types
    yaml_node_t *laser_types = get_yaml_node(&document, root, "laser_types");
    if (laser_types)
    {
        printf("Found laser_types section\n");
        for (int i = 0; i < laser_types->data.mapping.pairs.top - laser_types->data.mapping.pairs.start; i++)
        {
            yaml_node_pair_t *pair = laser_types->data.mapping.pairs.start + i;
            yaml_node_t *key_node = yaml_document_get_node(&document, pair->key);
            yaml_node_t *value_node = yaml_document_get_node(&document, pair->value);

            int type_idx = atoi((char *)key_node->data.scalar.value);
            printf("Loading laser type %d\n", type_idx);

            if (type_idx > 0 && type_idx < 11)
            {
                config->laser_types[type_idx].track_rate = get_yaml_float(&document, value_node, "track_rate", 60.0f);
                config->laser_types[type_idx].fov = get_yaml_float(&document, value_node, "fov", 5.0f);
                config->laser_types[type_idx].guidance_reduction = get_yaml_float(&document, value_node, "guidance_reduction", 5.0f);
                config->laser_types[type_idx].maximum_range = get_yaml_float(&document, value_node, "maximum_range", 4000.0f);
            }
        }
    }

    // Load entities section (seekers and lasers)
    yaml_node_t *entities = get_yaml_node(&document, root, "entities");
    if (entities)
    {
        printf("Found entities section\n");

        // Load seekers
        yaml_node_t *seekers = get_yaml_node(&document, entities, "seekers");
        if (seekers && seekers->type == YAML_SEQUENCE_NODE)
        {
            printf("Loading seekers array\n");
            for (int i = 0; i < seekers->data.sequence.items.top - seekers->data.sequence.items.start && i < MAX_SEEKERS; i++)
            {
                yaml_node_t *seeker = yaml_document_get_node(&document, seekers->data.sequence.items.start[i]);
                config->seekers[i] = get_yaml_int(&document, seeker, "type", 0);
                printf("Loaded seeker %d: type=%d\n", i, config->seekers[i]);
            }
        }

        // Load lasers
        yaml_node_t *lasers = get_yaml_node(&document, entities, "lasers");
        if (lasers && lasers->type == YAML_SEQUENCE_NODE)
        {
            printf("Loading lasers array\n");
            for (int i = 0; i < lasers->data.sequence.items.top - lasers->data.sequence.items.start && i < MAX_LASERS; i++)
            {
                yaml_node_t *laser = yaml_document_get_node(&document, lasers->data.sequence.items.start[i]);
                config->lasers[i] = get_yaml_int(&document, laser, "type", 0);
                printf("Loaded laser %d: type=%d\n", i, config->lasers[i]);
            }
        }
    }

cleanup:
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    fclose(file);
    return config;
}

// --- Apply configuration to simulation ---
void apply_config(AirSim *sim, SimConfig *config)
{
    sim->dt = config->dt;
    sim->max_time = config->max_time;
    sim->max_steps = config->max_steps;

    // Set aircraft state
    sim->aircraft.x = config->aircraft_x;
    sim->aircraft.y = config->aircraft_y;
    sim->aircraft.z = config->aircraft_z;
    sim->aircraft.vx = config->aircraft_vx;
    sim->aircraft.vy = config->aircraft_vy;
    sim->aircraft.vz = config->aircraft_vz;

    // Initialize seekers based on config->seekers array.
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        int t = config->seekers[i];
        if (t > 0)
        {
            sim->seekers[i].type = t;
            sim->seekers[i].fov = config->seeker_types[t].fov;
            sim->seekers[i].track_rate = config->seeker_types[t].track_rate;
            sim->seekers[i].acceleration = config->seeker_types[t].acceleration;
            sim->seekers[i].max_velocity = config->seeker_types[t].max_velocity;
            sim->seekers[i].lifetime = config->seeker_types[t].lifetime;
            sim->seekers[i].navigation_constant = config->seeker_types[t].navigation_constant;
            sim->seekers[i].active = true;
            sim->seekers[i].elapsed_time = 0.0f;
            // Position seekers relative to aircraft: for simplicity, spread them in front.
            float angle = ((float)i - (MAX_SEEKERS / 2)) * 5.0f * DEG2RAD;
            float distance = 1000.0f;
            sim->seekers[i].x = sim->aircraft.x + distance * cosf(angle);
            sim->seekers[i].y = sim->aircraft.y + distance * sinf(angle);
            sim->seekers[i].z = sim->aircraft.z - 200.0f;
            // Initial velocity towards aircraft.
            float dx = sim->aircraft.x - sim->seekers[i].x;
            float dy = sim->aircraft.y - sim->seekers[i].y;
            float dz = sim->aircraft.z - sim->seekers[i].z;
            float norm = sqrtf(dx * dx + dy * dy + dz * dz);
            sim->seekers[i].vx = (dx / norm) * (sim->seekers[i].max_velocity * 0.5f);
            sim->seekers[i].vy = (dy / norm) * (sim->seekers[i].max_velocity * 0.5f);
            sim->seekers[i].vz = (dz / norm) * (sim->seekers[i].max_velocity * 0.5f);
        }
        else
        {
            sim->seekers[i].active = false;
        }
    }

    // Initialize lasers based on config->lasers array.
    for (int i = 0; i < MAX_LASERS; i++)
    {
        int t = config->lasers[i];
        if (t > 0)
        {
            sim->lasers[i].type = t;
            sim->lasers[i].track_rate = config->laser_types[t].track_rate;
            sim->lasers[i].fov = config->laser_types[t].fov;
            sim->lasers[i].guidance_reduction = config->laser_types[t].guidance_reduction;
            sim->lasers[i].maximum_range = config->laser_types[t].maximum_range;
            sim->lasers[i].engaging_seeker = -1;
            sim->lasers[i].az = 0.0f;
            sim->lasers[i].el = 0.0f;
        }
        else
        {
            sim->lasers[i].type = 0;
            sim->lasers[i].engaging_seeker = -1;
        }
    }
}

// --- Helper Functions ---
static inline float compute_distance(float x1, float y1, float z1, float x2, float y2, float z2)
{
    float dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/**
 * Update aircraft position. For now, aircraft moves with constant velocity.
 */
void step_aircraft(AirSim *sim)
{
    sim->aircraft.x += sim->aircraft.vx * sim->dt;
    sim->aircraft.y += sim->aircraft.vy * sim->dt;
    sim->aircraft.z += sim->aircraft.vz * sim->dt;
}

/**
 * Update each seeker using a simple proportional navigation law.
 * Seekers adjust their velocity vector toward the aircraft.
 */
void step_seekers(AirSim *sim)
{
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        if (!sim->seekers[i].active)
            continue;
        Seeker *s = &sim->seekers[i];

        // Current relative position vector (R)
        float Rx = sim->aircraft.x - s->x;
        float Ry = sim->aircraft.y - s->y;
        float Rz = sim->aircraft.z - s->z;
        float R = sqrtf(Rx * Rx + Ry * Ry + Rz * Rz);
        if (R < 1.0f)
            R = 1.0f;

        // Determine if we're in terminal phase (R < 50m)
        bool terminal_phase = (R < 50.0f);

        // Unit LOS vector (los)
        float los_x = Rx / R;
        float los_y = Ry / R;
        float los_z = Rz / R;

        // Relative velocity vector (Vr)
        float Vr_x = sim->aircraft.vx - s->vx;
        float Vr_y = sim->aircraft.vy - s->vy;
        float Vr_z = sim->aircraft.vz - s->vz;

        // Closing velocity (Vc = -dR/dt = -(R·Vr)/R)
        float Vc = -(los_x * Vr_x + los_y * Vr_y + los_z * Vr_z);

        // True time-to-go estimation using closing velocity
        float tgo = R / Vc;
        if (tgo < 0.1f)
            tgo = 0.1f;

        // Calculate velocity component perpendicular to LOS
        float Vp_x = Vr_x - Vc * los_x;
        float Vp_y = Vr_y - Vc * los_y;
        float Vp_z = Vr_z - Vc * los_z;

        // LOS rate vector (ω = R×Vr/R²)
        float omega_x = (Ry * Vr_z - Rz * Vr_y) / (R * R);
        float omega_y = (Rz * Vr_x - Rx * Vr_z) / (R * R);
        float omega_z = (Rx * Vr_y - Ry * Vr_x) / (R * R);

        // For terminal phase, estimate target acceleration from velocity changes
        float at_x = 0;
        float at_y = 0;
        float at_z = 0;

        if (terminal_phase)
        {
            // Simple estimation of target acceleration
            at_x = sim->aircraft.vx * 0.1f; // Assume 10% of velocity as acceleration
            at_y = sim->aircraft.vy * 0.1f;
            at_z = sim->aircraft.vz * 0.1f;
        }

        if (sim->ticks % 10 == 0)
        {
            printf("\nSeeker %d Guidance Debug (t=%.2f):\n", i, sim->time);
            printf("  Position: (%.1f, %.1f, %.1f)\n", s->x, s->y, s->z);
            printf("  Range: %.1f  Vc: %.1f  Tgo: %.1f\n", R, Vc, tgo);
            printf("  Terminal Phase: %s\n", terminal_phase ? "YES" : "NO");
            printf("  LOS rates (xyz): %.3f, %.3f, %.3f\n", omega_x, omega_y, omega_z);
            printf("  Perp velocity: %.1f, %.1f, %.1f\n", Vp_x, Vp_y, Vp_z);
            float omega_mag = sqrtf(omega_x * omega_x + omega_y * omega_y + omega_z * omega_z);
            printf("  LOS rate magnitude: %.3f rad/s\n", omega_mag);
        }

        // Augmented PN acceleration command with terminal guidance
        float N = s->navigation_constant;
        if (terminal_phase)
        {
            N *= 2.0f; // Double navigation gain in terminal phase
        }
        float Np = N + 1.0f;

        // Base PN term
        float acc_x = N * Vc * omega_x;
        float acc_y = N * Vc * omega_y;
        float acc_z = N * Vc * omega_z;

        // Add target acceleration compensation
        acc_x += 0.5f * Np * at_x;
        acc_y += 0.5f * Np * at_y;
        acc_z += 0.5f * Np * at_z;

        // Add centripetal acceleration term
        float v_mag = sqrtf(s->vx * s->vx + s->vy * s->vy + s->vz * s->vz);
        if (v_mag > 1.0f)
        {
            float cent_factor = terminal_phase ? 1.5f : 1.0f; // Increase centripetal in terminal
            float cent_x = cent_factor * (v_mag * v_mag) * los_x / R;
            float cent_y = cent_factor * (v_mag * v_mag) * los_y / R;
            float cent_z = cent_factor * (v_mag * v_mag) * los_z / R;
            acc_x += cent_x;
            acc_y += cent_y;
            acc_z += cent_z;
        }

        // Add terminal guidance bias term to drive toward predicted collision
        if (terminal_phase)
        {
            float pred_x = sim->aircraft.x + sim->aircraft.vx * tgo;
            float pred_y = sim->aircraft.y + sim->aircraft.vy * tgo;
            float pred_z = sim->aircraft.z + sim->aircraft.vz * tgo;

            float bias_x = (pred_x - s->x) * 50.0f / R; // Scale bias by 50/R
            float bias_y = (pred_y - s->y) * 50.0f / R;
            float bias_z = (pred_z - s->z) * 50.0f / R;

            acc_x += bias_x;
            acc_y += bias_y;
            acc_z += bias_z;
        }

        if (sim->ticks % 10 == 0)
        {
            float acc_mag = sqrtf(acc_x * acc_x + acc_y * acc_y + acc_z * acc_z);
            printf("  Nav constants: N=%.1f, N'=%.1f\n", N, Np);
            printf("  Acc command (xyz): %.1f, %.1f, %.1f (mag: %.1f)\n",
                   acc_x, acc_y, acc_z, acc_mag);
        }

        // Update velocity using RK4 integration for better numerical stability
        float k1_vx, k1_vy, k1_vz;
        float k2_vx, k2_vy, k2_vz;
        float k3_vx, k3_vy, k3_vz;
        float k4_vx, k4_vy, k4_vz;
        float dt = sim->dt;

        // RK4 slopes
        k1_vx = acc_x;
        k1_vy = acc_y;
        k1_vz = acc_z;

        k2_vx = acc_x + 0.5f * dt * k1_vx;
        k2_vy = acc_y + 0.5f * dt * k1_vy;
        k2_vz = acc_z + 0.5f * dt * k1_vz;

        k3_vx = acc_x + 0.5f * dt * k2_vx;
        k3_vy = acc_y + 0.5f * dt * k2_vy;
        k3_vz = acc_z + 0.5f * dt * k2_vz;

        k4_vx = acc_x + dt * k3_vx;
        k4_vy = acc_y + dt * k3_vy;
        k4_vz = acc_z + dt * k3_vz;

        // Update velocity
        s->vx += (dt / 6.0f) * (k1_vx + 2 * k2_vx + 2 * k3_vx + k4_vx);
        s->vy += (dt / 6.0f) * (k1_vy + 2 * k2_vy + 2 * k3_vy + k4_vy);
        s->vz += (dt / 6.0f) * (k1_vz + 2 * k2_vz + 2 * k3_vz + k4_vz);

        // Limit velocity magnitude
        float speed = sqrtf(s->vx * s->vx + s->vy * s->vy + s->vz * s->vz);
        if (speed > s->max_velocity)
        {
            float scale = s->max_velocity / speed;
            s->vx *= scale;
            s->vy *= scale;
            s->vz *= scale;
        }

        // Update position
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->z += s->vz * dt;

        // Update lifetime
        s->elapsed_time += dt;
        if (s->elapsed_time >= s->lifetime)
            s->active = false;
    }
}

/**
 * Update lasers: slew toward target seeker if assigned.
 * When a laser is engaged and the target seeker is within FOV and range,
 * apply guidance reduction to the seeker (simulate jamming).
 */
void step_lasers(AirSim *sim)
{
    for (int i = 0; i < MAX_LASERS; i++)
    {
        if (sim->lasers[i].type == 0)
            continue;
        Laser *l = &sim->lasers[i];
        if (l->engaging_seeker >= 0 && l->engaging_seeker < MAX_SEEKERS)
        {
            Seeker *target = &sim->seekers[l->engaging_seeker];
            if (!target->active)
            {
                l->engaging_seeker = -1;
                continue;
            }
            // Compute desired angles from aircraft to seeker (for visualization and control).
            float dx = target->x - sim->aircraft.x;
            float dy = target->y - sim->aircraft.y;
            float dz = target->z - sim->aircraft.z;
            float range = sqrtf(dx * dx + dy * dy + dz * dz);
            float target_az = atan2f(dy, dx) * RAD2DEG;
            float ground_dist = sqrtf(dx * dx + dy * dy);
            float target_el = atan2f(dz, ground_dist) * RAD2DEG;

            // Slew current laser angles toward target angles.
            float az_diff = target_az - l->az;
            float el_diff = target_el - l->el;
            float delta_az = fabsf(az_diff);
            float delta_el = fabsf(el_diff);
            // Limit by track_rate.
            float max_angle_change = l->track_rate * sim->dt;
            if (fabsf(az_diff) > max_angle_change)
            {
                az_diff = (az_diff > 0 ? max_angle_change : -max_angle_change);
            }
            if (fabsf(el_diff) > max_angle_change)
            {
                el_diff = (el_diff > 0 ? max_angle_change : -max_angle_change);
            }
            l->az += az_diff;
            l->el += el_diff;

            // Check if target is within laser FOV and range.
            if (delta_az < l->fov && delta_el < l->fov && ground_dist <= l->maximum_range)
            {
                // print az and el diff for debugging
                printf("Laser %d: delta_az=%.2f, delta_el=%.2f\n", i, delta_az, delta_el);
                // Apply guidance reduction to the seeker: reduce its effective navigation constant.
                target->navigation_constant *= (1.0f - l->guidance_reduction * sim->dt);
                if (target->navigation_constant < 0.1f)
                {
                    target->navigation_constant = 0.1f;
                    // seeker defeated - turn it inactive
                    target->active = false;
                }
            }
        }
    }
}

/**
 * Process agent actions.
 * Action array length = MAX_LASERS. Each action is an integer:
 * -1: disengage laser, or a valid seeker index (0..MAX_SEEKERS-1) to engage.
 */
void process_actions(AirSim *sim)
{
    for (int i = 0; i < MAX_LASERS; i++)
    {
        int act = sim->actions[i];
        if (act < 0)
        {
            sim->lasers[i].engaging_seeker = -1;
        }
        else if (act >= 0 && act < MAX_SEEKERS)
        {
            if (sim->seekers[act].active)
            {
                sim->lasers[i].engaging_seeker = act;
            }
        }
    }
}

/**
 * Check terminal conditions:
 * - Simulation time exceeds max_time or ticks exceed max_steps.
 * - Any active seeker is within its lethal radius to the aircraft.
 * For simplicity, we assume lethal radius = 30.0f.
 */
bool check_terminal(AirSim *sim)
{
    if (sim->time >= sim->max_time || sim->ticks >= sim->max_steps)
    {
#ifdef DEBUG_PRINT
        printf("Simulation terminated: time=%.2f, max_time=%.2f, ticks=%d, max_steps=%d\n", sim->time, sim->max_time, sim->ticks, sim->max_steps);
#endif
        return true;
    }
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        if (sim->seekers[i].active)
        {
            float dist = compute_distance(sim->aircraft.x, sim->aircraft.y, sim->aircraft.z,
                                          sim->seekers[i].x, sim->seekers[i].y, sim->seekers[i].z);
            if (dist <= 30.0f)
                return true;
        }
    }
    return false;
}

/**
 * Compute reward: simple dense reward.
 * +0.1 for each tick survived, -100 if a seeker gets too close.
 */
void compute_reward(AirSim *sim)
{
    float reward = 0.1f;
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        if (sim->seekers[i].active)
        {
            float dist = compute_distance(sim->aircraft.x, sim->aircraft.y, sim->aircraft.z,
                                          sim->seekers[i].x, sim->seekers[i].y, sim->seekers[i].z);
            if (dist <= 30.0f)
            {
                reward = -100.0f;
                break;
            }
        }
    }
    sim->rewards[0] = reward;
}

/**
 * Update observations: fill the observation array with simulation state.
 * Format:
 * [ aircraft_x, aircraft_y, aircraft_z, aircraft_vx, aircraft_vy, aircraft_vz,
 *   For each seeker (10 features per seeker): one-hot encoded type (10 floats) + 3 relative pos + 3 relative vel,
 *   For each laser (13 features per laser): one-hot encoded type (10 floats) + engaging_seeker (float) + az + el ]
 */
void update_observations(AirSim *sim)
{
    int idx = 0;
    // Aircraft: 6 values.
    sim->observations[idx++] = sim->aircraft.x;
    sim->observations[idx++] = sim->aircraft.y;
    sim->observations[idx++] = sim->aircraft.z;
    sim->observations[idx++] = sim->aircraft.vx;
    sim->observations[idx++] = sim->aircraft.vy;
    sim->observations[idx++] = sim->aircraft.vz;

    // Seekers: For each, 10 (one-hot) + 3 + 3 = 16 values.
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        // One-hot encoding for seeker type: assume 10 types.
        for (int t = 1; t <= 10; t++)
        {
            sim->observations[idx++] = (sim->seekers[i].active && sim->seekers[i].type == t) ? 1.0f : 0.0f;
        }
        // Relative position (aircraft-centered)
        sim->observations[idx++] = sim->seekers[i].x - sim->aircraft.x;
        sim->observations[idx++] = sim->seekers[i].y - sim->aircraft.y;
        sim->observations[idx++] = sim->seekers[i].z - sim->aircraft.z;
        // Relative velocity
        sim->observations[idx++] = sim->seekers[i].vx - sim->aircraft.vx;
        sim->observations[idx++] = sim->seekers[i].vy - sim->aircraft.vy;
        sim->observations[idx++] = sim->seekers[i].vz - sim->aircraft.vz;
    }

    // Lasers: For each, 10 (one-hot) + 1 + 2 = 13 values.
    for (int i = 0; i < MAX_LASERS; i++)
    {
        for (int t = 1; t <= 10; t++)
        {
            sim->observations[idx++] = (sim->lasers[i].type == t) ? 1.0f : 0.0f;
        }
        sim->observations[idx++] = (float)sim->lasers[i].engaging_seeker;
        sim->observations[idx++] = sim->lasers[i].az;
        sim->observations[idx++] = sim->lasers[i].el;
    }
}

/**
 * Main simulation step.
 */
void step(AirSim *sim)
{
    process_actions(sim);
    step_aircraft(sim);
    step_seekers(sim);
    step_lasers(sim);
    compute_reward(sim);
    sim->terminals[0] = check_terminal(sim);
    update_observations(sim);
    sim->time += sim->dt;
    sim->ticks += 1;

    // Add current state to log buffer.
    // For brevity, we log only time and aircraft position.
    Log current_log = {sim->rewards[0], (float)sim->ticks};
    add_log(sim->log_buffer, &current_log);
}

/**
 * Reset simulation to initial state.
 */
void reset(AirSim *sim)
{
#ifdef DEBUG_PRINT
    printf("\n=== Resetting Simulation ===\n");
    printf("Config has:\n");
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        printf("Seeker %d type: %d\n", i, sim->config.seekers[i]);
    }
#endif
    sim->time = 0.0f;
    sim->ticks = 0;
    sim->terminal = false;

    // Reset aircraft from config
    sim->aircraft.x = sim->config.aircraft_x;
    sim->aircraft.y = sim->config.aircraft_y;
    sim->aircraft.z = sim->config.aircraft_z;
    sim->aircraft.vx = sim->config.aircraft_vx;
    sim->aircraft.vy = sim->config.aircraft_vy;
    sim->aircraft.vz = sim->config.aircraft_vz;

    // printf("\nInitializing seekers:\n");
    //  Reset seekers
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        int t = sim->config.seekers[i];
        // printf("Seeker %d: config type=%d\n", i, t);
        if (t > 0)
        {
            // printf("  Activating seeker %d with type %d\n", i, t);
            sim->seekers[i].type = t;
            sim->seekers[i].fov = sim->config.seeker_types[t].fov;
            sim->seekers[i].track_rate = sim->config.seeker_types[t].track_rate;
            sim->seekers[i].acceleration = sim->config.seeker_types[t].acceleration;
            sim->seekers[i].max_velocity = sim->config.seeker_types[t].max_velocity;
            sim->seekers[i].lifetime = sim->config.seeker_types[t].lifetime;
            sim->seekers[i].navigation_constant = sim->config.seeker_types[t].navigation_constant;
            sim->seekers[i].active = true;
            sim->seekers[i].elapsed_time = 0.0f;

            // Position seekers relative to aircraft
            float angle = ((float)i - (MAX_SEEKERS / 2)) * 10.0f * DEG2RAD;
            float distance = 1000.0f;
            sim->seekers[i].x = sim->aircraft.x + distance * cosf(angle);
            sim->seekers[i].y = sim->aircraft.y + distance * sinf(angle);
            sim->seekers[i].z = sim->aircraft.z - 200.0f;

            // Initialize velocities
            float dx = sim->aircraft.x - sim->seekers[i].x;
            float dy = sim->aircraft.y - sim->seekers[i].y;
            float dz = sim->aircraft.z - sim->seekers[i].z;
            float norm = sqrtf(dx * dx + dy * dy + dz * dz);
            sim->seekers[i].vx = (dx / norm) * (sim->seekers[i].max_velocity * 0.5f);
            sim->seekers[i].vy = (dy / norm) * (sim->seekers[i].max_velocity * 0.5f);
            sim->seekers[i].vz = (dz / norm) * (sim->seekers[i].max_velocity * 0.5f);
#ifdef DEBUG_PRINT
            printf("  Position=(%.1f, %.1f, %.1f)\n",
                   sim->seekers[i].x, sim->seekers[i].y, sim->seekers[i].z);
            printf("  Velocity=(%.1f, %.1f, %.1f)\n",
                   sim->seekers[i].vx, sim->seekers[i].vy, sim->seekers[i].vz);
#endif
        }
        else
        {
            // printf("  Deactivating seeker %d\n", i);
            sim->seekers[i].active = false;
        }
    }

    // Reset lasers.
    for (int i = 0; i < MAX_LASERS; i++)
    {
        int t = sim->config.lasers[i];
        if (t > 0)
        {
            sim->lasers[i].type = t;
            sim->lasers[i].track_rate = sim->config.laser_types[t].track_rate;
            sim->lasers[i].fov = sim->config.laser_types[t].fov;
            sim->lasers[i].guidance_reduction = sim->config.laser_types[t].guidance_reduction;
            sim->lasers[i].maximum_range = sim->config.laser_types[t].maximum_range;
            sim->lasers[i].engaging_seeker = -1;
            sim->lasers[i].az = 0.0f;
            sim->lasers[i].el = 0.0f;
        }
        else
        {
            sim->lasers[i].type = 0;
            sim->lasers[i].engaging_seeker = -1;
        }
    }
    update_observations(sim);
    sim->rewards[0] = 0.0f;
    sim->terminals[0] = 0;

    // Clear log buffer.
    sim->log_buffer->idx = 0;
}

/**
 * Allocate memory for simulation buffers.
 */
void allocate(AirSim *sim)
{
    // Compute observation size: 6 (aircraft) + MAX_SEEKERS*16 + MAX_LASERS*13.
    int obs_size = 6 + MAX_SEEKERS * 16 + MAX_LASERS * 13;
    sim->observations = (float *)calloc(obs_size, sizeof(float));
    sim->actions = (int *)calloc(MAX_LASERS, sizeof(int));
    sim->rewards = (float *)calloc(1, sizeof(float));
    sim->terminals = (unsigned char *)calloc(1, sizeof(unsigned char));
    sim->log_buffer = allocate_logbuffer(LOG_BUFFER_SIZE);

    // Initialize simulation time.
    sim->time = 0.0f;
    sim->ticks = 0;
    sim->terminal = false;
}

/**
 * Free allocated buffers.
 */
void free_allocated(AirSim *sim)
{
    if (sim)
    {
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
    }
}

/**
 * Initialize simulation with config loaded.
 */
void init(AirSim *sim)
{
    reset(sim);
}

#endif

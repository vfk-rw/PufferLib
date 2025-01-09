#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "raylib.h"
#include <stdio.h> // Added to declare printf and fprintf
#include "log.h"

#define MAX_THREATS 10
#define MAX_LASERS 5
#define PI 3.14159265358979323846f
//#define DEBUG_TERMINAL
//#define DEBUG_PRINT
#define LOG_BUFFER_SIZE 1024
#define MAX_SEEKER_ERROR_SCALE 50.0f  // Make error much larger for visibility
#define RAD2DEG_SAFE 57.2957795131f  // 180/pi
#define DEG2RAD_SAFE 0.0174532925f   // pi/180

typedef struct Threat {
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

typedef struct Laser {
    int type;
    int engaging_threat;
    float az, el;
    float track_rate;
    float fov;
    float guidance_reduction;
    float maximum_range;
} Laser;

typedef struct AirSim {
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
    float* observations;
    int* actions;
    float* rewards;
    unsigned char* terminals;

    LogBuffer* log_buffer;
    Log log;
} AirSim;

LogBuffer* allocate_logbuffer(int size) {
    LogBuffer* logs = (LogBuffer*)calloc(1, sizeof(LogBuffer));
    logs->logs = (Log*)calloc(size, sizeof(Log));
    logs->length = size;
    logs->idx = 0;
    return logs;
}

void free_logbuffer(LogBuffer* buffer) {
    free(buffer->logs);
    free(buffer);
}

void add_log(LogBuffer* logs, Log* log) {
    if (logs->idx == logs->length) {
        return;
    }
    logs->logs[logs->idx] = *log;
    logs->idx += 1;
    //printf("Log: %f, %f, %f\n", log->episode_return, log->episode_length, log->score);
}

Log aggregate_and_clear(LogBuffer* logs) {
    Log log = {0};
    if (logs->idx == 0) {
        return log;
    }
    for (int i = 0; i < logs->idx; i++) {
        log.episode_return += logs->logs[i].episode_return;
        log.episode_length += logs->logs[i].episode_length;
    }
    log.episode_return /= logs->idx;
    log.episode_length /= logs->idx;
    logs->idx = 0;
    return log;
}

// Helper functions
float compute_distance(float x1, float y1, float z1, float x2, float y2, float z2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

void compute_angles_to_target(float x1, float y1, float z1, 
                            float x2, float y2, float z2,
                            float* az, float* el) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    
    *az = atan2f(dy, dx) * RAD2DEG_SAFE;
    float ground_distance = sqrtf(dx*dx + dy*dy);
    *el = atan2f(dz, ground_distance) * RAD2DEG_SAFE;
}

// Core simulation functions
void step_aircraft(AirSim* sim) {
    // Update aircraft position based on velocity
    sim->aircraft_x += sim->aircraft_vx * sim->dt;
    sim->aircraft_y += sim->aircraft_vy * sim->dt;
    sim->aircraft_z += sim->aircraft_vz * sim->dt;
    
    // Keep moving at constant velocity
    sim->aircraft_vx = 100.0f;  // Maintain forward speed
}

void engage_threats(AirSim* sim) {
    #ifdef DEBUG_PRINT
    printf("Debug: Engaging threats\n");
    #endif
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0) {
            #ifdef DEBUG_PRINT
            printf("Debug: Threat %d at (%f, %f, %f), engaged %d\n", 
                   i, sim->threats[i].x, sim->threats[i].y, sim->threats[i].z, sim->threats[i].engaged);
            #endif
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z
            );
            if (dist <= sim->threats[i].engagement_radius) {
                sim->threats[i].engaged = true;
            }
        }
    }
}

void step_threats(AirSim* sim) {
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0 && sim->threats[i].engaged) {
            // Get true angle to aircraft
            float true_az, true_el;
            compute_angles_to_target(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                &true_az, &true_el
            );
            
            // Add error based on guidance_gain
            float error_scale = (1.0f - sim->threats[i].guidance_gain) * MAX_SEEKER_ERROR_SCALE;
            float error = error_scale * sinf(sim->time * 2.0f);  // Oscillating error
            
            // Only apply error to azimuth for now
            float apparent_az = true_az + error;
            float apparent_el = true_el + error;
            
            // Update seeker head angles with track rate limit
            float max_turn = sim->threats[i].track_rate * sim->dt;
            
            // Calculate shortest angle difference for azimuth
            float az_diff = fmodf(apparent_az - sim->threats[i].az + 540.0f, 360.0f) - 180.0f;
            float el_diff = apparent_el - sim->threats[i].el;
            
            if (az_diff > max_turn) az_diff = max_turn;
            if (az_diff < -max_turn) az_diff = -max_turn;
            if (el_diff > max_turn) el_diff = max_turn;
            if (el_diff < -max_turn) el_diff = -max_turn;
            
            // Update angles
            sim->threats[i].az = fmodf(sim->threats[i].az + az_diff + 360.0f, 360.0f);
            sim->threats[i].el = fminf(89.0f, fmaxf(-89.0f, sim->threats[i].el + el_diff));
            
            // Always accelerate in direction of seeker head
            float az_rad = sim->threats[i].az * DEG2RAD_SAFE;
            float el_rad = sim->threats[i].el * DEG2RAD_SAFE;
            
            float ground_comp = cosf(el_rad);
            float dir_x = ground_comp * cosf(az_rad);
            float dir_y = ground_comp * sinf(az_rad);
            float dir_z = sinf(el_rad);
            
            // Full acceleration always
            sim->threats[i].vx += sim->threats[i].acceleration * dir_x * sim->dt;
            sim->threats[i].vy += sim->threats[i].acceleration * dir_y * sim->dt;
            sim->threats[i].vz += sim->threats[i].acceleration * dir_z * sim->dt;
            
            // Limit velocity magnitude
            float current_velocity = sqrtf(
                sim->threats[i].vx * sim->threats[i].vx +
                sim->threats[i].vy * sim->threats[i].vy +
                sim->threats[i].vz * sim->threats[i].vz
            );
            
            if (current_velocity > sim->threats[i].max_velocity) {
                float scale = sim->threats[i].max_velocity / current_velocity;
                sim->threats[i].vx *= scale;
                sim->threats[i].vy *= scale;
                sim->threats[i].vz *= scale;
            }
            
            // Update position
            sim->threats[i].x += sim->threats[i].vx * sim->dt;
            sim->threats[i].y += sim->threats[i].vy * sim->dt;
            sim->threats[i].z += sim->threats[i].vz * sim->dt;
            
            // Check for ground collision
            if (sim->threats[i].z <= 0.0f) {
                sim->threats[i].type = 0;  // Deactivate threat
                sim->threats[i].lifetime = 0.0f;
                continue;
            }
            
            // Update lifetime
            sim->threats[i].lifetime -= sim->dt;
            if (sim->threats[i].lifetime <= 0) {
                sim->threats[i].type = 0;
            }
        }
    }
}

void slew_lasers(AirSim* sim) {
    for (int i = 0; i < MAX_LASERS; i++) {
        if (sim->lasers[i].type > 0 && sim->lasers[i].engaging_threat >= 0) {
            int threat_idx = sim->lasers[i].engaging_threat;
            #ifdef DEBUG_PRINT
            printf("[Step %d] SlewLasers: Laser %d attempting to engage threat %d (type=%d)\n", sim->ticks,
                   i, threat_idx, sim->threats[threat_idx].type);
            #endif
            if (sim->threats[threat_idx].type > 0) {
                float target_az, target_el;
                compute_angles_to_target(
                    sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                    sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z,
                    &target_az, &target_el
                );
                #ifdef DEBUG_PRINT
                printf("[Step %d] SlewLasers: Laser %d -> Threat %d, Current(az=%.1f, el=%.1f) Target(az=%.1f, el=%.1f)\n", sim->ticks,
                       i, threat_idx, sim->lasers[i].az, sim->lasers[i].el, target_az, target_el);
                #endif
                // Calculate angle differences
                float az_diff = fmodf(target_az - sim->lasers[i].az + 540.0f, 360.0f) - 180.0f;
                float el_diff = target_el - sim->lasers[i].el;
                
                // Apply slew limits
                float max_angle = sim->lasers[i].track_rate * sim->dt;
                if (az_diff > max_angle) az_diff = max_angle;
                if (az_diff < -max_angle) az_diff = -max_angle;
                if (el_diff > max_angle) el_diff = max_angle;
                if (el_diff < -max_angle) el_diff = -max_angle;
                
                // Update angles
                sim->lasers[i].az = fmodf(sim->lasers[i].az + az_diff + 360.0f, 360.0f);
                sim->lasers[i].el = fminf(90.0f, fmaxf(-90.0f, sim->lasers[i].el + el_diff));
            } else {
                // Don't reset engagement - let manual control handle this
                #ifdef DEBUG_PRINT
                printf("[Step %d] SlewLasers: Laser %d target threat %d is inactive\n", sim->ticks,
                       i, threat_idx);
                #endif

            }
        }
    }
}

void apply_countermeasures(AirSim* sim) {
    for (int i = 0; i < MAX_LASERS; i++) {
        if (sim->lasers[i].type > 0 && sim->lasers[i].engaging_threat >= 0) {
            int threat_idx = sim->lasers[i].engaging_threat;
            if (sim->threats[threat_idx].type > 0) {
                float target_az, target_el;
                compute_angles_to_target(
                    sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                    sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z,
                    &target_az, &target_el
                );
                
                // Check if threat is in FOV
                float az_diff = fabsf(fmodf(target_az - sim->lasers[i].az + 540.0f, 360.0f) - 180.0f);
                float el_diff = fabsf(target_el - sim->lasers[i].el);
                
                if (az_diff <= sim->lasers[i].fov && el_diff <= sim->lasers[i].fov) {
                    float dist = compute_distance(
                        sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                        sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z
                    );
                    
                    if (dist <= sim->lasers[i].maximum_range) {
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

char check_terminal(AirSim* sim) {
    // Check if any threat is within lethal radius
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0 && sim->threats[i].engaged) {
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z
            );
            if (dist <= sim->threats[i].lethal_radius) {
                return 1;
            }
        }
    }
    
    // Check if all active threats have expired
    bool all_threats_expired = false;
    bool had_threats = false;
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0) {
            had_threats = true;
            if (sim->threats[i].lifetime > 0) {
                all_threats_expired = false;
                break;
            }
        }
    }
    if (had_threats && all_threats_expired) {
        return 1;
    }
    
    // Check time limits
    if (sim->time >= sim->max_time || sim->ticks >= sim->max_steps) {
        return 1;
    }
    
    return 0;
}

void compute_rewards(AirSim* sim) {
    float reward = 0.0f;
    
    // Reward for surviving until threats expire
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type == 0 && sim->threats[i].lifetime <= 0) {
            reward += 10.0f;
        }
    }
    
    // Penalty for terminal state (threat within lethal radius)
    if (sim->terminal) {
        reward -= 100.0f;
    }
    
    sim->rewards[0] = reward;
}
void process_actions(AirSim* sim) {
    for (int i = 0; i < MAX_LASERS; i++) {
        if (sim->lasers[i].engaging_threat == -1) {
            if (sim->actions[i] >= 0 && sim->actions[i] < MAX_THREATS && 
                sim->threats[sim->actions[i]].type > 0) {
                sim->lasers[i].engaging_threat = sim->actions[i];
            }
        }
    }
}

void update_observations(AirSim* sim) {
    int obs_idx = 0;
    sim->observations[obs_idx++] = sim->aircraft_x;
    sim->observations[obs_idx++] = sim->aircraft_y;
    sim->observations[obs_idx++] = sim->aircraft_z;
    sim->observations[obs_idx++] = sim->aircraft_vx;
    sim->observations[obs_idx++] = sim->aircraft_vy;
    sim->observations[obs_idx++] = sim->aircraft_vz;
    
    for (int i = 0; i < MAX_THREATS; i++) {
        sim->observations[obs_idx++] = (float)sim->threats[i].type;
        sim->observations[obs_idx++] = sim->threats[i].x;
        sim->observations[obs_idx++] = sim->threats[i].y;
        sim->observations[obs_idx++] = sim->threats[i].z;
        sim->observations[obs_idx++] = sim->threats[i].vx;
        sim->observations[obs_idx++] = sim->threats[i].vy;
        sim->observations[obs_idx++] = sim->threats[i].vz;
        sim->observations[obs_idx++] = sim->threats[i].guidance_gain;  // Replace HP with guidance_gain
    }
    
    for (int i = 0; i < MAX_LASERS; i++) {
        sim->observations[obs_idx++] = (float)sim->lasers[i].type;
        sim->observations[obs_idx++] = (float)sim->lasers[i].engaging_threat;
        sim->observations[obs_idx++] = sim->lasers[i].az;
        sim->observations[obs_idx++] = sim->lasers[i].el;
    }
}
// Main simulation step
void step(AirSim* sim) {
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
    if (sim->terminal) {
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
void reset(AirSim* sim) {
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
    for (int i = 0; i < MAX_THREATS; i++) {
        sim->threats[i].type = (i < 3) ? 1 : 0;  // Start with 3 active threats
        sim->threats[i].engaged = false;
        
        // Position threats in front of aircraft in a spread pattern
        float angle = ((float)i - 1.5f) * PI / 6.0f;  // Spread threats across 60 degrees
        float rand_z = ((float)rand() / (float)RAND_MAX - 0.5f) * 200.0f;  // Limit Z variation to ±100m
        
        sim->threats[i].x = sim->aircraft_x + sim->initial_distance * cosf(angle);
        sim->threats[i].y = sim->aircraft_y + sim->initial_distance * sinf(angle);
        sim->threats[i].z = 300.0f + rand_z;  // have threats start at ~300m altitude
        
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
            &sim->threats[i].az, &sim->threats[i].el
        );
    }
    
    // Reset lasers
    for (int i = 0; i < MAX_LASERS; i++) {
        sim->lasers[i].type = 0;
        //if (i < 2) sim->lasers[i].type = 1;     // Start with 2 active lasers
        sim->lasers[i].engaging_threat = -1;
        sim->lasers[i].az = 0.0f;
        sim->lasers[i].el = 0.0f;
        sim->lasers[i].track_rate = 60.0f;
        sim->lasers[i].fov = 5.0f;
        sim->lasers[i].guidance_reduction = 1.0f;  // Reduces guidance by 100% per second
        sim->lasers[i].maximum_range = 4000.0f;
    }
    
    update_observations(sim);
    sim->rewards[0] = 0.0f;
    sim->terminals[0] = false;
}

void init(AirSim* sim) {
    reset(sim);
}

// Allocate simulation buffers
void allocate(AirSim* sim) {
    // Allocate memory before initialization
    int obs_size = 6 + 8 * MAX_THREATS + 4 * MAX_LASERS;  // Fix observation size calculation
    sim->observations = (float*)calloc(obs_size, sizeof(float));
    sim->actions = (int*)calloc(MAX_LASERS, sizeof(int));
    sim->rewards = (float*)calloc(1, sizeof(float));
    sim->terminals = (unsigned char*)calloc(1, sizeof(unsigned char));
    sim->log_buffer = allocate_logbuffer(LOG_BUFFER_SIZE);

    // Verify allocations
    if (!sim->observations || !sim->actions || !sim->rewards || 
        !sim->terminals || !sim->log_buffer) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(1);
    }

    // Initialize after memory allocation
    init(sim);
}

// Free simulation buffers
void free_allocated(AirSim* sim) {
    if (!sim) return;
    if (sim->observations) free(sim->observations);
    if (sim->actions) free(sim->actions);
    if (sim->rewards) free(sim->rewards);
    if (sim->terminals) free(sim->terminals);
    if (sim->log_buffer) free_logbuffer(sim->log_buffer);
    
    sim->observations = NULL;
    sim->actions = NULL;
    sim->rewards = NULL;
    sim->terminals = NULL;
    sim->log_buffer = NULL;
}


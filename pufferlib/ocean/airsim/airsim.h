#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "raylib.h"
#include <stdio.h> // Added to declare printf and fprintf

#define MAX_THREATS 10
#define MAX_LASERS 5
#define PI 3.14159265358979323846f
//#define DEBUG_TERMINAL
//#define DEBUG_PRINT
#define LOG_BUFFER_SIZE 1024

typedef struct Log Log;
struct Log {
    float episode_return;
    float episode_length;
};

typedef struct LogBuffer LogBuffer;
struct LogBuffer {
    Log* logs;
    int length;
    int idx;
};

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

// Threat state structure
typedef struct Threat {
    int type;                  // 0 for inactive, 1+ for active
    bool engaged;              // True if threat is launched
    float x, y, z;            // Position (meters)
    float vx, vy, vz;         // Velocity (m/s)
    int hp;                   // Health points
    float engagement_radius;   // Meters
    float lethal_radius;      // Meters
    float acceleration;       // m/s^2
    float max_velocity;       // m/s
    float lifetime;           // Seconds remaining
} Threat;

// Laser state structure
typedef struct Laser {
    int type;                 // 0 for inactive, 1+ for active
    int engaging_threat;      // -1: not engaging, 0+: threat index
    float az;                 // Azimuth (degrees)
    float el;                 // Elevation (degrees)
    float track_rate;         // Degrees/second
    float fov;                // Field of view (degrees)
    float damage_per_second;  // Damage rate
    float maximum_range;      // Maximum effective range (meters)
} Laser;

// Main simulation state
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
    float time;              // Current simulation time
    int ticks;              // Tick counter (previously steps)
    char terminal;            // Terminal state flag
    
    // Aircraft state
    float aircraft_x, aircraft_y, aircraft_z;     // Position
    float aircraft_vx, aircraft_vy, aircraft_vz;  // Velocity
    
    // Environment parameters
    float dt;                 // Time step size
    float max_time;           // Maximum simulation time
    int max_steps;            // Maximum simulation steps
    
    // Environment interface
    float* observations;      // Observation buffer
    int* actions;            // Action buffer
    float* rewards;          // Reward buffer
    unsigned char* terminals; // Terminal state buffer

    LogBuffer* log_buffer;
    Log log;
} AirSim;

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
    
    *az = atan2f(dy, dx) * 180.0f / PI;
    float ground_distance = sqrtf(dx*dx + dy*dy);
    *el = atan2f(dz, ground_distance) * 180.0f / PI;
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
            // Pro-nav guidance towards aircraft
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z
            );
            
            float lead_time = dist / (sim->threats[i].max_velocity + 1e-6f);
            float target_x = sim->aircraft_x + sim->aircraft_vx * lead_time;
            float target_y = sim->aircraft_y + sim->aircraft_vy * lead_time;
            float target_z = sim->aircraft_z + sim->aircraft_vz * lead_time;
            
            float dx = target_x - sim->threats[i].x;
            float dy = target_y - sim->threats[i].y;
            float dz = target_z - sim->threats[i].z;
            float norm = sqrtf(dx*dx + dy*dy + dz*dz) + 1e-6f;
            
            float current_velocity = sqrtf(
                sim->threats[i].vx * sim->threats[i].vx +
                sim->threats[i].vy * sim->threats[i].vy +
                sim->threats[i].vz * sim->threats[i].vz
            );
            
            float accel_scale = fminf(
                sim->threats[i].acceleration,
                (sim->threats[i].max_velocity - current_velocity) / sim->dt
            );
            
            // Update velocities
            sim->threats[i].vx += accel_scale * dx / norm * sim->dt;
            sim->threats[i].vy += accel_scale * dy / norm * sim->dt;
            sim->threats[i].vz += accel_scale * dz / norm * sim->dt;
            
            // Update positions
            sim->threats[i].x += sim->threats[i].vx * sim->dt;
            sim->threats[i].y += sim->threats[i].vy * sim->dt;
            sim->threats[i].z += sim->threats[i].vz * sim->dt;
            
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
            printf("[Step %d] SlewLasers: Laser %d attempting to engage threat %d (type=%d)\n", sim->ticks,
                   i, threat_idx, sim->threats[threat_idx].type);
                   
            if (sim->threats[threat_idx].type > 0) {
                float target_az, target_el;
                compute_angles_to_target(
                    sim->aircraft_x, sim->aircraft_y, sim->aircraft_z,
                    sim->threats[threat_idx].x, sim->threats[threat_idx].y, sim->threats[threat_idx].z,
                    &target_az, &target_el
                );
                
                printf("[Step %d] SlewLasers: Laser %d -> Threat %d, Current(az=%.1f, el=%.1f) Target(az=%.1f, el=%.1f)\n", sim->ticks,
                       i, threat_idx, sim->lasers[i].az, sim->lasers[i].el, target_az, target_el);
                
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
                printf("[Step %d] SlewLasers: Laser %d target threat %d is inactive\n", sim->ticks,
                       i, threat_idx);
            }
        }
    }
}

void apply_damage(AirSim* sim) {
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
                        sim->threats[threat_idx].hp -= sim->lasers[i].damage_per_second * sim->dt;
                        if (sim->threats[threat_idx].hp <= 0) {
                            sim->threats[threat_idx].type = 0;
                        }
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
                #ifdef DEBUG_TERMINAL
                printf("Terminal condition met: Threat %d within lethal radius (%f <= %f)\n", 
                       i, dist, sim->threats[i].lethal_radius);
                #endif
                return 1;
            }
        }
    }
    
    // Check time limits
    if (sim->time >= sim->max_time || sim->ticks >= sim->max_steps) {
        #ifdef DEBUG_TERMINAL
        printf("Terminal condition met: Time or step limit reached.\n");
        #endif
        return 1;
    }
    
    return 0;
}

// Environment interface functions
void update_observations(AirSim* sim) {
    #ifdef DEBUG_PRINT
    printf("Debug: Updating observations, aircraft at (%f, %f, %f)\n", 
           sim->aircraft_x, sim->aircraft_y, sim->aircraft_z);
    #endif
    int obs_idx = 0;
    sim->observations[obs_idx++] = sim->aircraft_x;
    sim->observations[obs_idx++] = sim->aircraft_y;
    sim->observations[obs_idx++] = sim->aircraft_z;
    sim->observations[obs_idx++] = sim->aircraft_vx;
    sim->observations[obs_idx++] = sim->aircraft_vy;
    sim->observations[obs_idx++] = sim->aircraft_vz;
    
    for (int i = 0; i < MAX_THREATS; i++) {
        #ifdef DEBUG_PRINT
        printf("Debug: Threat %d at (%f, %f, %f), type %d\n", 
               i, sim->threats[i].x, sim->threats[i].y, sim->threats[i].z, sim->threats[i].type);
        #endif
        sim->observations[obs_idx++] = (float)sim->threats[i].type;
        sim->observations[obs_idx++] = sim->threats[i].x;
        sim->observations[obs_idx++] = sim->threats[i].y;
        sim->observations[obs_idx++] = sim->threats[i].z;
        sim->observations[obs_idx++] = sim->threats[i].vx;
        sim->observations[obs_idx++] = sim->threats[i].vy;
        sim->observations[obs_idx++] = sim->threats[i].vz;
        sim->observations[obs_idx++] = (float)sim->threats[i].hp;
    }
    
    // Laser states
    for (int i = 0; i < MAX_LASERS; i++) {
        sim->observations[obs_idx++] = (float)sim->lasers[i].type;
        sim->observations[obs_idx++] = (float)sim->lasers[i].engaging_threat;
        sim->observations[obs_idx++] = sim->lasers[i].az;
        sim->observations[obs_idx++] = sim->lasers[i].el;
    }
}

void process_actions(AirSim* sim) {
    // Actions encode which laser engages which threat
    for (int i = 0; i < MAX_LASERS; i++) {
        printf("[Step %d] ProcessActions: Laser %d current_target=%d, action=%d\n", sim->ticks,
               i, sim->lasers[i].engaging_threat, sim->actions[i]);
        
        // Don't override manual engagement with action buffer
        if (sim->lasers[i].engaging_threat == -1) {
            // Only set engaging_threat if action is a valid threat index
            if (sim->actions[i] >= 0 && sim->actions[i] < MAX_THREATS && 
                sim->threats[sim->actions[i]].type > 0) {
                sim->lasers[i].engaging_threat = sim->actions[i];
                printf("[Step %d] ProcessActions: Laser %d engaging threat %d from action buffer\n", 
                      sim->ticks, i, sim->actions[i]);
            }
        } else {
            printf("[Step %d] ProcessActions: Laser %d keeping manual engagement on threat %d\n",
                  sim->ticks, i, sim->lasers[i].engaging_threat);
        }
    }
}

void compute_rewards(AirSim* sim) {
    float reward = 0.0f;
    
    // Reward for destroying threats
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type == 0 && sim->threats[i].hp <= 0) {
            reward += 10.0f;
        }
    }
    
    // Penalty for terminal state (threat within lethal radius)
    if (sim->terminal) {
        reward -= 100.0f;
    }
    
    sim->rewards[0] = reward;
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
    apply_damage(sim);
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
        sim->threats[i].x = sim->aircraft_x + sim->initial_distance + 500.0f * cosf(angle);
        sim->threats[i].y = sim->aircraft_y + sim->initial_distance * sinf(angle);
        sim->threats[i].z = sim->aircraft_z + ((float)rand() / (float)RAND_MAX - 0.5f) * 1000.0f;
        
        sim->threats[i].vx = 0.0f;
        sim->threats[i].vy = 0.0f;
        sim->threats[i].vz = 0.0f;
        sim->threats[i].hp = 100;
        sim->threats[i].engagement_radius = sim->engagement_radius;
        sim->threats[i].lethal_radius = 30.0f;
        sim->threats[i].acceleration = sim->threat_acceleration;
        sim->threats[i].max_velocity = sim->threat_max_velocity;        
        sim->threats[i].lifetime = 17.0f;
    }
    
    // Reset lasers
    for (int i = 0; i < MAX_LASERS; i++) {
        sim->lasers[i].type = 0;
        if (i < 2) sim->lasers[i].type = 1;     // Start with 2 active lasers
        sim->lasers[i].engaging_threat = -1;
        sim->lasers[i].az = 0.0f;
        sim->lasers[i].el = 0.0f;
        sim->lasers[i].track_rate = 30.0f;
        sim->lasers[i].fov = 5.0f;
        sim->lasers[i].damage_per_second = 2000.0f;
        sim->lasers[i].maximum_range = 4000.0f;
    }
    
    update_observations(sim);
    sim->rewards[0] = 0.0f;
    sim->terminals[0] = false;
}

// Initialize simulation
void init(AirSim* sim) {
    // Don't initialize parameters - set by Python
    // sim->dt = 0.01f;
    // sim->max_time = 60.0f;
    // sim->max_steps = 6000;
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
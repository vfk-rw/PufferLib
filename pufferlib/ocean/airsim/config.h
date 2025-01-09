#ifndef AIRSIM_CONFIG_H
#define AIRSIM_CONFIG_H

#include "airsim.h"

typedef struct SimConfig {
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
    struct {
        float track_rate;
        float fov;
        float guidance_reduction;
        float maximum_range;
    } laser_types[10];  // Support up to 10 types

    struct {
        float fov;
        float track_rate;
        float guidance_gain;
        float engagement_radius;
        float lethal_radius;
        float acceleration;
        float max_velocity;
        float lifetime;
    } threat_types[10];  // Support up to 10 types

    // Entity definitions
    struct {
        int type;  // 0 = inactive, 1+ = type index
    } lasers[MAX_LASERS];

    struct {
        int type;  // 0 = inactive, 1+ = type index
    } threats[MAX_THREATS];
} SimConfig;

// Load config from YAML file
SimConfig* load_config(const char* filename);

// Apply config to simulation
void apply_config(AirSim* sim, SimConfig* config);

// Free config
void free_config(SimConfig* config);

#endif

# cython: language_level=3
# distutils: language = c

cimport numpy as cnp
from libc.stdlib cimport calloc, free
import os

cdef extern from "airsim.h":

    int LOG_BUFFER_SIZE

    ctypedef struct Log:
        float episode_return;
        float episode_length;

    ctypedef struct LogBuffer
    LogBuffer* allocate_logbuffer(int)
    void free_logbuffer(LogBuffer*)
    Log aggregate_and_clear(LogBuffer*)

    ctypedef struct Threat:
        int type
        bint engaged
        float x, y, z
        float vx, vy, vz
        float az, el
        float fov
        float track_rate
        float guidance_gain
        float engagement_radius
        float lethal_radius
        float acceleration
        float max_velocity
        float lifetime

    ctypedef struct Laser:
        int type
        int engaging_threat
        float az, el
        float track_rate
        float fov
        float guidance_reduction
        float maximum_range
    
    ctypedef struct AirSim:
        float initial_distance
        float aircraft_speed
        float threat_acceleration
        float threat_max_velocity 
        float engagement_radius

        Threat* threats
        Laser* lasers
        float time
        int steps
        char terminal

        float aircraft_x, aircraft_y, aircraft_z
        float aircraft_vx, aircraft_vy, aircraft_vz

        float dt
        float max_time
        int max_steps

        float* observations
        int* actions
        float* rewards
        unsigned char* terminals

        LogBuffer* log_buffer
        Log log

    void init(AirSim* env)
    void allocate(AirSim* sim)
    void free_allocated(AirSim* sim)
    void reset(AirSim* sim)
    void step(AirSim* sim)

cdef class CyAirSim:
    cdef:
        AirSim* envs
        LogBuffer* logs
        int num_envs
        float initial_distance
        float aircraft_speed
        float threat_acceleration
        float threat_max_velocity
        float engagement_radius
        float max_time
        float dt
        
    def __init__(self,
                  float[:, :] observations,
                  int[:] actions,
                  float[:] rewards,
                  unsigned char[:] terminals,
                  int num_envs,
                  float initial_distance,
                  float aircraft_speed,
                  float threat_acceleration,
                  float threat_max_velocity,
                  float engagement_radius,
                  float max_time,
                  float dt):

        self.num_envs = num_envs
        self.envs = <AirSim*> calloc(num_envs, sizeof(AirSim))

        self.logs = allocate_logbuffer(LOG_BUFFER_SIZE)
        cdef int i
        for i in range(num_envs):
            self.envs[i] = AirSim(
                observations = &observations[i, 0],
                actions = &actions[i],
                rewards = &rewards[i],
                terminals = &terminals[i],
                log_buffer=self.logs,
                initial_distance = initial_distance,
                aircraft_speed = aircraft_speed,
                threat_acceleration = threat_acceleration,
                threat_max_velocity = threat_max_velocity,
                engagement_radius = engagement_radius,
                max_time = max_time,
                dt = dt,
            )
            init(&self.envs[i])

    def reset(self):
        cdef int i
        for i in range(self.num_envs):
            reset(&self.envs[i])

    def step(self):
        cdef int i
        for i in range(self.num_envs):
            step(&self.envs[i])

    def close(self):
        free(self.envs)
        free(self.logs)

    def log(self):
        cdef Log log = aggregate_and_clear(self.logs)
        return log

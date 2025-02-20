# cython: language_level=3
# distutils: language = c

cimport numpy as cnp
from libc.stdlib cimport calloc, free
import os

cdef extern from "log.h":
    int LOG_BUFFER_SIZE
    ctypedef struct Log:
        float episode_return
        float episode_length
    ctypedef struct LogBuffer:
        Log* logs
        int length
        int idx
    LogBuffer* allocate_logbuffer(int)
    void free_logbuffer(LogBuffer*)
    Log aggregate_and_clear(LogBuffer*)

cdef extern from "airsim2.h":
    ctypedef struct AirSim:
        float* observations
        int* actions
        float* rewards
        unsigned char* terminals
        LogBuffer* log_buffer
        Log log
        float time
        int ticks
        bint terminal
    
    void init(AirSim* sim)
    void reset(AirSim* sim)
    void step(AirSim* sim)

    ctypedef struct SimConfig:
        pass
    SimConfig* load_config(const char* filename)
    void apply_config(AirSim* sim, SimConfig* config)
    void free_allocated(AirSim* sim)
    
    void reset_aircraft_pos(AirSim *sim, float x, float y, float z)
    void reset_aircraft_vel(AirSim *sim, float vx, float vy, float vz)
    void reset_seeker(AirSim *sim, int idx, int type, float x, float y, float z, float vx, float vy, float vz)
    void reset_laser(AirSim *sim, int idx, int type, float az, float el)
    void apply_seeker_type_config(AirSim *sim, int type, float fov, float track_rate, float acceleration, float max_velocity, float lifetime, float nav_const)
    void apply_laser_type_config(AirSim *sim, int type, float track_rate, float fov, float guidance_reduction, float maximum_range)

cdef class CyAirSim:
    cdef:
        AirSim* envs
        LogBuffer* logs
        int num_envs
        
    def __init__(self,
                 float[:, :] observations,
                 int[:, :] actions,
                 float[:] rewards,
                 unsigned char[:] terminals,
                 int num_envs=1,
                 str config_file="example.yaml"):

        if num_envs <= 0 or num_envs > 1000:
            raise ValueError("num_envs must be between 1 and 1000")
            
        self.num_envs = num_envs
        self.logs = allocate_logbuffer(LOG_BUFFER_SIZE)
        if self.logs == NULL:
            raise MemoryError("Failed to allocate log buffer")

        # Load config file
        cdef SimConfig* config = load_config(config_file.encode('utf-8'))
        if config == NULL:
            free_logbuffer(self.logs)
            raise RuntimeError(f"Failed to load config file: {config_file}")

        # Just allocate the array of AirSim structs
        self.envs = <AirSim*>calloc(num_envs, sizeof(AirSim))
        if self.envs == NULL:
            free_logbuffer(self.logs)
            free(config)
            raise MemoryError("Failed to allocate AirSim environments")

        # Initialize each environment with pointers to numpy arrays
        cdef int i
        for i in range(num_envs):
            self.envs[i].observations = &observations[i, 0]
            self.envs[i].actions = &actions[i, 0]
            self.envs[i].rewards = &rewards[i]
            self.envs[i].terminals = &terminals[i]
            self.envs[i].log_buffer = self.logs
            
            # Apply config to each environment
            apply_config(&self.envs[i], config)
            init(&self.envs[i])

        # Free the config after applying to all environments
        free(config)

    def __dealloc__(self):
        if self.envs != NULL:
            free(self.envs)
        if self.logs != NULL:
            free_logbuffer(self.logs)

    def reset(self):
        cdef int i
        for i in range(self.num_envs):
            reset(&self.envs[i])

    def step(self):
        cdef int i
        for i in range(self.num_envs):
            step(&self.envs[i])

    def close(self):
        # Remove close() since __dealloc__ handles cleanup
        pass

    def log(self):
        cdef Log log = aggregate_and_clear(self.logs)
        return {"episode_return": log.episode_return, 
                "episode_length": log.episode_length}

    @property 
    def sim(self):
        # Make the pointer accessible but not the struct itself
        return <size_t><void*>&self.envs[0]

    def set_aircraft_pos(self, float x, float y, float z):
        reset_aircraft_pos(&self.envs[0], x, y, z)
        
    def set_aircraft_vel(self, float vx, float vy, float vz):
        reset_aircraft_vel(&self.envs[0], vx, vy, vz)
        
    def set_seeker(self, int idx, int type, float x, float y, float z, float vx, float vy, float vz):
        reset_seeker(&self.envs[0], idx, type, x, y, z, vx, vy, vz)
        
    def set_laser(self, int idx, int type, float az, float el):
        reset_laser(&self.envs[0], idx, type, az, el)
        
    def configure_seeker_type(self, int type, float fov, float track_rate, float acceleration, 
                            float max_velocity, float lifetime, float nav_const):
        apply_seeker_type_config(&self.envs[0], type, fov, track_rate, acceleration, 
                               max_velocity, lifetime, nav_const)
        
    def configure_laser_type(self, int type, float track_rate, float fov, 
                           float guidance_reduction, float maximum_range):
        apply_laser_type_config(&self.envs[0], type, track_rate, fov, 
                              guidance_reduction, maximum_range)
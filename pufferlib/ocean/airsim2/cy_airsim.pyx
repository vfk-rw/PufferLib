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

cdef extern from "airsim.h":
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
                 int num_envs=1):

        if num_envs <= 0 or num_envs > 1000:
            raise ValueError("num_envs must be between 1 and 1000")
            
        self.num_envs = num_envs
        self.logs = allocate_logbuffer(LOG_BUFFER_SIZE)
        if self.logs == NULL:
            raise MemoryError("Failed to allocate log buffer")

        # Just allocate the array of AirSim structs
        self.envs = <AirSim*>calloc(num_envs, sizeof(AirSim))
        if self.envs == NULL:
            free_logbuffer(self.logs)
            raise MemoryError("Failed to allocate AirSim environments")

        # Initialize each environment with pointers to numpy arrays
        cdef int i
        for i in range(num_envs):
            self.envs[i].observations = &observations[i, 0]
            self.envs[i].actions = &actions[i, 0]
            self.envs[i].rewards = &rewards[i]
            self.envs[i].terminals = &terminals[i]
            self.envs[i].log_buffer = self.logs
            init(&self.envs[i])

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
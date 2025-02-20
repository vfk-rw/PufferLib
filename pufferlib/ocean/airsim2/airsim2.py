import numpy as np
import gymnasium
import time
import pufferlib
from cy_airsim2 import CyAirSim

class SimConfig:
    def __init__(self, env):
        self.env = env
        self._c_env = env.c_env

    @property
    def aircraft(self):
        return AircraftConfig(self._c_env)
    
    @property
    def seekers(self):
        return SeekerConfig(self._c_env)
    
    @property
    def lasers(self):
        return LaserConfig(self._c_env)

class AircraftConfig:
    def __init__(self, c_env):
        self._c_env = c_env
        
    def set_position(self, x, y, z):
        self._c_env.set_aircraft_pos(x, y, z)
        
    def set_velocity(self, vx, vy, vz):
        self._c_env.set_aircraft_vel(vx, vy, vz)

class SeekerConfig:
    def __init__(self, c_env):
        self._c_env = c_env
    
    def set_type_params(self, type_id, fov=None, track_rate=None, acceleration=None,
                       max_velocity=None, lifetime=None, nav_const=None):
        """Update seeker type parameters"""
        # Default values if not provided
        defaults = {
            'fov': 30.0,
            'track_rate': 100.0,
            'acceleration': 400.0,
            'max_velocity': 1000.0,
            'lifetime': 17.0,
            'nav_const': 3.0
        }
        
        params = {
            'fov': fov if fov is not None else defaults['fov'],
            'track_rate': track_rate if track_rate is not None else defaults['track_rate'],
            'acceleration': acceleration if acceleration is not None else defaults['acceleration'],
            'max_velocity': max_velocity if max_velocity is not None else defaults['max_velocity'],
            'lifetime': lifetime if lifetime is not None else defaults['lifetime'],
            'nav_const': nav_const if nav_const is not None else defaults['nav_const']
        }
        
        self._c_env.configure_seeker_type(
            type_id, 
            params['fov'],
            params['track_rate'],
            params['acceleration'],
            params['max_velocity'],
            params['lifetime'],
            params['nav_const']
        )
    
    def set_seeker(self, idx, type_id, pos=(0,0,0), vel=(0,0,0)):
        """Configure a specific seeker instance"""
        x,y,z = pos
        vx,vy,vz = vel
        self._c_env.set_seeker(idx, type_id, x, y, z, vx, vy, vz)

class LaserConfig:
    def __init__(self, c_env):
        self._c_env = c_env
    
    def set_type_params(self, type_id, track_rate=None, fov=None, 
                       guidance_reduction=None, maximum_range=None):
        """Update laser type parameters"""
        defaults = {
            'track_rate': 60.0,
            'fov': 5.0,
            'guidance_reduction': 5.0,
            'maximum_range': 4000.0
        }
        
        params = {
            'track_rate': track_rate if track_rate is not None else defaults['track_rate'],
            'fov': fov if fov is not None else defaults['fov'],
            'guidance_reduction': guidance_reduction if guidance_reduction is not None else defaults['guidance_reduction'],
            'maximum_range': maximum_range if maximum_range is not None else defaults['maximum_range']
        }
        
        self._c_env.configure_laser_type(
            type_id,
            params['track_rate'],
            params['fov'],
            params['guidance_reduction'],
            params['maximum_range']
        )
    
    def set_laser(self, idx, type_id, az=0.0, el=0.0):
        """Configure a specific laser instance"""
        self._c_env.set_laser(idx, type_id, az, el)

class AirDefense(pufferlib.PufferEnv):
    def __init__(
        self,
        num_envs=1,
        render_mode=None,
        config_file="example.yaml",  # Add config_file parameter
    ):
        obs_size = 6 + 10*16 + 5*13  # aircraft + seekers + lasers
        self.single_observation_space = gymnasium.spaces.Box(
            low=-np.inf, high=np.inf, shape=(obs_size,), dtype=np.float32
        )
        self.single_action_space = gymnasium.spaces.MultiDiscrete([11] * 5)  # 5 lasers, each with 11 possible actions
        self.render_mode = render_mode
        self.num_agents = num_envs

        self.human_action = None
        self.tick = 0
        self.report_interval = 10

        # Initialize shared numpy arrays for environments
        self.observations = np.zeros((num_envs, obs_size), dtype=np.float32)
        self.actions = np.zeros((num_envs, 5), dtype=np.int32)  # Match action space size
        self.rewards = np.zeros(num_envs, dtype=np.float32)
        self.terminals = np.zeros(num_envs, dtype=np.uint8)

        super().__init__()
        self.c_env = CyAirSim(
            observations=self.observations,
            actions=self.actions,
            rewards=self.rewards,
            terminals=self.terminals,
            num_envs=num_envs,
            config_file=config_file  # Pass config file to CyAirSim
        )
        self.config = SimConfig(self)


    def reset(self, seed=None):
        self.tick = 0
        self.c_env.reset()
        self.config.seekers.set_seeker(3, type_id=1, pos=(10,0,1000), vel=(200,0,0))
        return self.observations, []

    def step(self, actions):
        # Ensure actions is the right shape
        if isinstance(actions, np.ndarray):
            self.actions[:] = actions.reshape(self.num_agents, -1)
        else:
            # Handle list of actions
            self.actions[0] = np.array(actions, dtype=np.int32)
        
        self.c_env.step()

        info = []
        if self.tick % self.report_interval == 0:
            log = self.c_env.log()
            if log["episode_length"] > 0:
                info.append(log)

        self.tick += 1
        return (self.observations, self.rewards, self.terminals, self.truncations, info)

    def render(self):
        if self.render_mode is not None:
            pass  # Implement visualization if needed

    def close(self):
        self.c_env.close()

def test_performance(num_envs=512, timeout=10, action_cache_size=1024):
    print(f"\nTesting with {num_envs} environments...")
    try:
        env = AirDefense(num_envs=num_envs)
        env.reset()
        tick = 0
        actions = np.random.randint(-1, 10, (action_cache_size, num_envs, 5), dtype=np.int32)

        start = time.time()
        while time.time() - start < timeout:
            obs, rewards, terminals, truncs, info = env.step(actions[tick % action_cache_size])
            tick += 1

        total_time = time.time() - start
        steps_per_second = env.num_agents * tick / total_time
        print(f"Total Steps: {tick * num_envs:,}")
        print(f"Time: {total_time:.2f} seconds")
        print(f"Steps per second: {steps_per_second:,.0f}")
    finally:
        env.close()

def test_rewards(num_envs=1, cache_size=2001):
    print(f"\nTesting with {num_envs} environments...")
    try:
        env = AirDefense(num_envs=num_envs)
        env.reset()
        tick = 0
        actions = np.random.randint(-1, 10, (cache_size, num_envs, 5), dtype=np.int32)
        rewards = np.zeros(cache_size, dtype=np.float32)
        terminals = np.zeros(cache_size, dtype=np.uint8)
        start = time.time()
        while tick < cache_size:
            obs, rewards[tick], terminals[tick], truncs, info = env.step(actions[tick % cache_size])
            tick += 1
            
        total_time = time.time() - start
        steps_per_second = env.num_agents * tick / total_time
        total_rewards = np.sum(rewards)
        first_terminal = np.argmax(terminals) if np.any(terminals) else -1
        print(f"Total Steps: {tick * num_envs:,}")
        print(f"Time: {total_time:.2f} seconds")
        print(f"Steps per second: {steps_per_second:,.0f}")
        print(f"Total rewards: {total_rewards}")
        print(f"First terminal state at step: {first_terminal}")
    finally:
        env.close()

if __name__ == "__main__":
    print("Running environment tests...")
    test_rewards(num_envs=1)
    exit(0)
    for n_envs in [1, 2, 4, 64, 512]:
        test_performance(num_envs=n_envs, timeout=2)
        print(f"done with {n_envs} environment")
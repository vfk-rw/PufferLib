import numpy as np
import gymnasium
import time
import pufferlib
from cy_airsim import CyAirSim

class AirDefense(pufferlib.PufferEnv):
    def __init__(
        self,
        num_envs=1,
        render_mode=None,
        config_file="example.yaml",
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
            num_envs=num_envs
        )

    def reset(self, seed=None):
        self.tick = 0
        self.c_env.reset()
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
            env.step(actions[tick % action_cache_size])
            tick += 1

        total_time = time.time() - start
        steps_per_second = env.num_agents * tick / total_time
        print(f"Total Steps: {tick * num_envs:,}")
        print(f"Time: {total_time:.2f} seconds")
        print(f"Steps per second: {steps_per_second:,.0f}")
    finally:
        env.close()

if __name__ == "__main__":
    print("Running environment tests...")
    for n_envs in [1, 2, 4, 64, 512]:
        test_performance(num_envs=n_envs, timeout=2)
        print(f"done with {n_envs} environment")
import numpy as np
import gymnasium
import time
import pufferlib
from pufferlib.ocean.airsim.cy_airsim import CyAirSim


class AirDefense(pufferlib.PufferEnv):
    def __init__(
        self,
        num_envs=1,
        render_mode=None,
        initial_distance=2000.0,
        aircraft_speed=100.0,
        threat_acceleration=400.0,
        threat_max_velocity=1000.0,
        engagement_radius=2500.0,
        max_time=60.0,
        dt=0.001,
        buf=None,
    ):
        # 6 for aircraft (pos, vel) +
        # MAX_THREATS * 8 (type, pos, vel, hp) +
        # MAX_LASERS * 4 (type, engaging_threat, az, el)
        obs_size = 6 + 10 * 8 + 5 * 4
        self.single_observation_space = gymnasium.spaces.Box(
            low=-np.inf, high=np.inf, shape=(obs_size,), dtype=np.float32
        )
        self.single_action_space = gymnasium.spaces.Discrete(5)
        self.render_mode = render_mode
        self.num_agents = num_envs

        self.human_action = None
        self.tick = 0
        self.report_interval = 10

        super().__init__(buf)
        self.c_envs = CyAirSim(
            self.observations,
            self.actions,
            self.rewards,
            self.terminals,
            num_envs,
            initial_distance=initial_distance,
            aircraft_speed=aircraft_speed,
            threat_acceleration=threat_acceleration,
            threat_max_velocity=threat_max_velocity,
            engagement_radius=engagement_radius,
            max_time=max_time,
            dt=dt,
        )

    def reset(self, seed=None):
        self.tick = 0
        self.c_envs.reset()
        return self.observations, []

    def step(self, actions):
        self.actions[:] = actions
        self.c_envs.step()

        info = []
        if self.tick % self.report_interval == 0:
            log = self.c_envs.log()
            if log["episode_length"] > 0:
                info.append(log)

        self.tick += 1
        return (self.observations, self.rewards, self.terminals, self.truncations, info)

    def render(self):
        self.c_envs.render()

    def close(self):
        self.c_envs.close()


def test_performance(num_envs=1, timeout=10, action_cache_size=1024):
    env = AirDefense(num_envs=num_envs)
    env.reset()
    tick = 0
    actions = np.random.randint(0, 4, (action_cache_size, env.num_agents))

    start = time.time()
    while time.time() - start < timeout:
        env.step(actions[tick % action_cache_size])
        tick += 1

    total_time = time.time() - start
    steps_per_second = env.num_agents * tick / total_time
    print(f"Environments: {num_envs}")
    print(f"Total Steps: {tick * num_envs:,}")
    print(f"Time: {total_time:.2f} seconds")
    print(f"Steps per second: {steps_per_second:,.0f}")


def test_sim(ticks=2980, action_cache_size=1024):
    env = AirDefense(
        num_envs=1,
        initial_distance=1500.0,
        aircraft_speed=100.0,
        threat_acceleration=400.0,
        threat_max_velocity=1000.0,
        engagement_radius=2500.0,
    )
    obs, _ = env.reset()

    for tick in range(ticks):
        obs = obs.reshape(1, -1)
        aircraft_pos = obs[0, :3]

        threat_data = []
        for i in range(10):
            offset = 6 + i * 8
            threat_type = obs[0, offset]
            if threat_type > 0:
                pos = obs[0, offset + 1 : offset + 4]
                dist = np.linalg.norm(pos - aircraft_pos)
                threat_data.append((i, dist))

        threat_data.sort(key=lambda x: x[1])

        # Select a valid action based on threat_data
        actions_to_take = np.full(
            (env.num_agents,), 0, dtype=np.int32
        )  # Default to action 0 (e.g., 'still')
        for i in range(min(len(threat_data), env.num_agents)):
            # actions_to_take[i] = threat_data[i][0] % 5  # Ensure action is within [0, 4]
            actions_to_take[i] = 0

        obs, reward, term, trunc, info = env.step(actions=[0])

        if tick % 10 == 0:
            print(f"\nTick {tick}")
            print(
                f"Aircraft: ({obs[0,0]:.0f}, {obs[0,1]:.0f}, {obs[0,2]:.0f}) term: {term}"
            )

            for i in range(10):
                offset = 6 + i * 8
                if obs[0, offset] > 0:
                    pos = obs[0, offset + 1 : offset + 4]
                    dist = np.linalg.norm(pos - aircraft_pos)
                    print(
                        f"Threat {i}: ({pos[0]:.0f}, {pos[1]:.0f}, {pos[2]:.0f}), Distance: {dist:.0f}"
                    )
            if term or trunc:
                print("Simulation ended")
                break


if __name__ == "__main__":
    print("Running performance test...")
    test_performance()
    # print("\nRunning simulation test...")
    # test_sim()

#include "airsim.h"
#include "ffmpeg.h"
#include <stdio.h>
#include <stdlib.h>

// Window and drawing settings
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define GRID_SIZE 100.0f
#define PIXELS_PER_METER 0.2f

// Laser color definitions for up to MAX_LASERS
#define MAX_LASER_COLORS 5
static const Color LASER_COLORS[MAX_LASER_COLORS] = {
    (Color){0, 128, 255, 255},
    (Color){128, 0, 255, 255},
    (Color){0, 64, 196, 255},
    (Color){128, 0, 196, 255},
    (Color){64, 64, 255, 255}};

// Game state structure to hold UI and camera info
typedef struct GameState
{
    bool paused;
    bool show_help;
    bool show_debug;
    bool show_threat_paths;
    bool recording;
    int selected_threat;
    int active_laser;
    bool laser_view[MAX_LASERS]; // one flag per laser
    Camera2D camera;
    float zoom_target;
} GameState;

// Forward declarations for UI drawing functions
void draw_help_panel();
void draw_debug_info(AirSim *sim);
void update_camera(GameState *state);
int select_next_threat(AirSim *sim, int current, bool forward);
int select_next_laser(AirSim *sim, int current, bool forward);

// Reset UI state to defaults
void reset_ui_state(GameState *state)
{
    state->paused = false;
    state->show_help = false;
    state->show_debug = false;
    state->show_threat_paths = true;
    state->recording = false;
    state->selected_threat = 0;
    state->active_laser = 0;
    for (int i = 0; i < MAX_LASERS; i++)
    {
        state->laser_view[i] = true;
    }
    state->camera.target = (Vector2){0, 0};
    state->camera.offset = (Vector2){WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f};
    state->camera.rotation = 0.0f;
    state->camera.zoom = PIXELS_PER_METER * 4.0f;
    state->zoom_target = state->camera.zoom;
}

// Draw help panel overlay listing all commands
void draw_help_panel()
{
    const char *help_text[] = {
        "F1 - Toggle Help",
        "F2 - REC",
        "SPACE - Pause/Resume",
        "R - Reset Simulation",
        "P - Toggle Threat Paths",
        "TAB - Debug Info",
        "MMB/Wheel - Pan/Zoom Camera",
        "LEFT/RIGHT - Select Threat",
        "UP/DOWN - Select Laser",
        "ENTER - Engage Laser",
        "BACKSPACE - Disengage Laser",
        "1-5 - Toggle Laser View",
        "CLICK - Step Forward",
        "ESC - Quit"};
    int num_lines = sizeof(help_text) / sizeof(help_text[0]);
    int padding = 10;
    int panel_width = 300;
    int panel_height = num_lines * 20 + 2 * padding;
    DrawRectangle(WINDOW_WIDTH - panel_width - padding, padding, panel_width, panel_height, (Color){255, 255, 255, 230});
    for (int i = 0; i < num_lines; i++)
    {
        DrawText(help_text[i], WINDOW_WIDTH - panel_width, padding + i * 20, 20, DARKGRAY);
    }
}

// Draw simple debug info overlay
void draw_debug_info(AirSim *sim)
{
    char buffer[128];
    sprintf(buffer, "Ticks: %d  Time: %.2f", sim->ticks, sim->time);
    DrawText(buffer, 10, 10, 20, BLACK);
}

// Update camera panning/zoom based on mouse input
void update_camera(GameState *state)
{
    // Zoom with mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0)
    {
        state->zoom_target *= (1.0f + wheel * 0.1f);
        if (state->zoom_target < 0.1f)
            state->zoom_target = 0.1f;
        if (state->zoom_target > 10.0f)
            state->zoom_target = 10.0f;
    }
    // Pan with middle mouse button
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        Vector2 delta = GetMouseDelta();
        state->camera.offset.x += delta.x;
        state->camera.offset.y += delta.y;
    }
    // Smoothly update camera zoom toward target
    state->camera.zoom += (state->zoom_target - state->camera.zoom) * 0.1f;
}

// Cycle through active threats. If 'forward' is true, go to the next; else previous.
int select_next_threat(AirSim *sim, int current, bool forward)
{
    int idx = current;
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        idx = forward ? (current + 1 + i) % MAX_SEEKERS : (current - 1 - i + MAX_SEEKERS) % MAX_SEEKERS;
        if (sim->seekers[idx].active)
            return idx;
    }
    return current;
}

// Cycle through active lasers.
int select_next_laser(AirSim *sim, int current, bool forward)
{
    int idx = current;
    for (int i = 0; i < MAX_LASERS; i++)
    {
        idx = forward ? (current + 1 + i) % MAX_LASERS : (current - 1 - i + MAX_LASERS) % MAX_LASERS;
        if (sim->lasers[idx].type > 0)
            return idx;
    }
    return current;
}

// Main simulation and visualization loop
int main(int argc, char **argv)
{
    // Load configuration (default "example.yaml", or use argv[1])
    const char *config_file = "example.yaml";
    if (argc > 1)
        config_file = argv[1];
    SimConfig *config = load_sim_config(config_file);
    if (!config)
    {
        fprintf(stderr, "Failed to load config file: %s\n", config_file);
        return -1;
    }

    // Initialize simulation
    AirSim sim;
    sim.config = *config; // Copy config into simulation.
    free(config);
    allocate(&sim);
    apply_config(&sim, &sim.config);
    reset(&sim);

    // Initialize raylib window.
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "AirSim Visualizer");
    SetTargetFPS(60);

    // Initialize game state and camera.
    GameState state;
    reset_ui_state(&state);

    // FFmpeg recording handle (for REC command)
    FFMPEG *ffmpeg_handle = NULL;

    // Main loop
    while (!WindowShouldClose())
    {
        // --- Process Input Commands ---
        // F1 - Toggle Help overlay.
        if (IsKeyPressed(KEY_F1))
        {
            state.show_help = !state.show_help;
        }
        // F2 - Toggle REC (recording)
        if (IsKeyPressed(KEY_F2))
        {
            if (!state.recording)
            {
                ffmpeg_handle = ffmpeg_start_rendering(WINDOW_WIDTH, WINDOW_HEIGHT, 30, NULL);
                if (ffmpeg_handle)
                    state.recording = true;
            }
            else
            {
                if (ffmpeg_handle)
                {
                    ffmpeg_end_rendering(ffmpeg_handle, false);
                    ffmpeg_handle = NULL;
                }
                state.recording = false;
            }
        }
        // SPACE - Pause/Resume simulation.
        if (IsKeyPressed(KEY_SPACE))
        {
            state.paused = !state.paused;
        }
        // R - Reset Simulation.
        if (IsKeyPressed(KEY_R))
        {
            reset(&sim);
            reset_ui_state(&state);
        }
        // P - Toggle Threat Paths (for additional visualization, if implemented).
        if (IsKeyPressed(KEY_P))
        {
            state.show_threat_paths = !state.show_threat_paths;
        }
        // TAB - Toggle Debug Info.
        if (IsKeyPressed(KEY_TAB))
        {
            state.show_debug = !state.show_debug;
        }
        // LEFT/RIGHT → - Select Threat.
        if (IsKeyPressed(KEY_RIGHT))
        {
            state.selected_threat = select_next_threat(&sim, state.selected_threat, true);
        }
        if (IsKeyPressed(KEY_LEFT))
        {
            state.selected_threat = select_next_threat(&sim, state.selected_threat, false);
        }
        // UP/DOWN → - Select Laser.
        if (IsKeyPressed(KEY_UP))
        {
            state.active_laser = select_next_laser(&sim, state.active_laser, false);
        }
        if (IsKeyPressed(KEY_DOWN))
        {
            state.active_laser = select_next_laser(&sim, state.active_laser, true);
        }
        // ENTER - Engage Laser (set active laser to target selected threat).
        if (IsKeyPressed(KEY_ENTER))
        {
            if (sim.seekers[state.selected_threat].active)
            {
                sim.lasers[state.active_laser].engaging_seeker = state.selected_threat;
            }
        }
        // BACKSPACE - Disengage Laser.
        if (IsKeyPressed(KEY_BACKSPACE))
        {
            sim.lasers[state.active_laser].engaging_seeker = -1;
        }
        // 1-5 - Toggle Laser View for each laser.
        if (IsKeyPressed(KEY_ONE))
            state.laser_view[0] = !state.laser_view[0];
        if (IsKeyPressed(KEY_TWO))
            state.laser_view[1] = !state.laser_view[1];
        if (IsKeyPressed(KEY_THREE))
            state.laser_view[2] = !state.laser_view[2];
        if (IsKeyPressed(KEY_FOUR))
            state.laser_view[3] = !state.laser_view[3];
        if (IsKeyPressed(KEY_FIVE))
            state.laser_view[4] = !state.laser_view[4];
        // CLICK (Left Mouse Button) - Step simulation one tick.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            step(&sim);
        }
        // MMB and Mouse Wheel for Pan/Zoom Camera.
        update_camera(&state);

        // Automatically step simulation if not paused and simulation not terminal.
        if (!state.paused && !sim.terminal)
        {
            step(&sim);
        }

        // --- Rendering ---
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode2D(state.camera);

        // Draw a grid for reference.
        for (int x = -WINDOW_WIDTH; x < WINDOW_WIDTH * 2; x += GRID_SIZE)
        {
            Vector2 start = {x, -WINDOW_HEIGHT};
            Vector2 end = {x, WINDOW_HEIGHT * 2};
            DrawLineV(start, end, LIGHTGRAY);
        }
        for (int y = -WINDOW_HEIGHT; y < WINDOW_HEIGHT * 2; y += GRID_SIZE)
        {
            Vector2 start = {-WINDOW_WIDTH, y};
            Vector2 end = {WINDOW_WIDTH * 2, y};
            DrawLineV(start, end, LIGHTGRAY);
        }

        // Draw aircraft at the origin.
        Vector2 ac_pos = {0, 0};
        DrawCircleV(ac_pos, 10, BLUE);

        // Draw seekers relative to aircraft.
        for (int i = 0; i < MAX_SEEKERS; i++)
        {
            if (!sim.seekers[i].active)
                continue;
            Vector2 seeker_pos = {sim.seekers[i].x - sim.aircraft.x, sim.seekers[i].y - sim.aircraft.y};
            Color col = RED;
            if (i == state.selected_threat)
            {
                col = MAROON;
                DrawCircleV(seeker_pos, 15, col);
            }
            else
            {
                DrawCircleV(seeker_pos, 8, col);
            }
        }

        // Draw lasers (if their view is toggled on)
        for (int i = 0; i < MAX_LASERS; i++)
        {
            if (sim.lasers[i].type == 0)
                continue;
            if (!state.laser_view[i])
                continue;
            // Draw an arc representing laser FOV centered on the aircraft.
            float range = sim.lasers[i].maximum_range * state.camera.zoom;
            float start_angle = sim.lasers[i].az - sim.lasers[i].fov;
            float end_angle = sim.lasers[i].az + sim.lasers[i].fov;
            DrawCircleSector(ac_pos, range, start_angle, end_angle, 32, LASER_COLORS[i % MAX_LASER_COLORS]);
            // Draw a line showing current laser angle.
            Vector2 line_end = {ac_pos.x + range * cosf(sim.lasers[i].az * DEG2RAD),
                                ac_pos.y + range * sinf(sim.lasers[i].az * DEG2RAD)};
            DrawLineV(ac_pos, line_end, DARKGREEN);
        }

        EndMode2D();

        // Draw HUD information.
        DrawText(TextFormat("Time: %.2f", sim.time), 10, 10, 20, BLACK);
        DrawText(TextFormat("Tick: %d", sim.ticks), 10, 35, 20, BLACK);
        if (state.paused)
            DrawText("PAUSED", 10, 60, 20, RED);

        if (state.show_debug)
            draw_debug_info(&sim);
        if (state.show_help)
            draw_help_panel();

        EndDrawing();

        // If recording, capture the screen and send to ffmpeg.
        if (state.recording && ffmpeg_handle)
        {
            Image screen = LoadImageFromScreen();
            if (screen.data)
            {
                ffmpeg_send_frame_flipped(ffmpeg_handle, screen.data, screen.width, screen.height);
                UnloadImage(screen);
            }
        }

        // ESC - Quit.
        if (IsKeyPressed(KEY_ESCAPE))
            break;
    }

    // End recording if active.
    if (state.recording && ffmpeg_handle)
    {
        ffmpeg_end_rendering(ffmpeg_handle, false);
    }

    free_allocated(&sim);
    CloseWindow();
    return 0;
}

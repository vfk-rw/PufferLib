#include "airsim2.h"
#include "ffmpeg.h"
#include <stdio.h>
#include <stdlib.h>

#define DEBUG_PRINT

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

// Helper function to get laser color with custom alpha
Color get_laser_color(int laser_idx, unsigned char alpha)
{
    Color c = LASER_COLORS[laser_idx % MAX_LASER_COLORS];
    c.a = alpha;
    return c;
}

// Reset UI state to defaults - modified to start paused
void reset_ui_state(GameState *state)
{
    state->paused = true; // Changed from false to true
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

// Add debug print function
void print_debug_state(AirSim *sim)
{
    printf("\n=== Simulation State (Tick %d, Time %.2f) ===\n", sim->ticks, sim->time);
    printf("Aircraft: pos=(%.1f, %.1f, %.1f) vel=(%.1f, %.1f, %.1f)\n",
           sim->aircraft.x, sim->aircraft.y, sim->aircraft.z,
           sim->aircraft.vx, sim->aircraft.vy, sim->aircraft.vz);

    // Print seeker states
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        if (sim->seekers[i].active)
        {
            printf("Seeker %d: type=%d pos=(%.1f, %.1f, %.1f) vel=(%.1f, %.1f, %.1f)\n",
                   i, sim->seekers[i].type,
                   sim->seekers[i].x, sim->seekers[i].y, sim->seekers[i].z,
                   sim->seekers[i].vx, sim->seekers[i].vy, sim->seekers[i].vz);
        }
    }

    // Print laser states
    for (int i = 0; i < MAX_LASERS; i++)
    {
        if (sim->lasers[i].type > 0)
        {
            printf("Laser %d: type=%d engaging=%d az=%.1f el=%.1f\n",
                   i, sim->lasers[i].type, sim->lasers[i].engaging_seeker,
                   sim->lasers[i].az, sim->lasers[i].el);
        }
    }
    printf("=====================================\n");
}

// Improved draw_laser_arc with transparency and better visuals
void draw_laser_arc(Camera2D *cam, AirSim *sim, int laser_idx, bool is_active)
{
    Vector2 pos = GetWorldToScreen2D((Vector2){sim->aircraft.x, sim->aircraft.y}, *cam);
    Laser *laser = &sim->lasers[laser_idx];

    float az_rad = laser->az * DEG2RAD;
    float fov_rad = laser->fov * DEG2RAD;
    float range = laser->maximum_range * cam->zoom;

    // Draw laser arc
    float start_angle = (az_rad - fov_rad) * RAD2DEG;
    float end_angle = (az_rad + fov_rad) * RAD2DEG;

    // Get laser colors with transparency
    Color fill_color = get_laser_color(laser_idx, is_active ? 32 : 16);
    Color line_color = get_laser_color(laser_idx, is_active ? 255 : 128);
    Color text_color = get_laser_color(laser_idx, is_active ? 255 : 128);

    DrawCircleSector(pos, range, start_angle, end_angle, 32, fill_color);
    DrawCircleSectorLines(pos, range, start_angle, end_angle, 32, line_color);

    // Draw centerline
    Vector2 end = {
        pos.x + cosf(az_rad) * range,
        pos.y + sinf(az_rad) * range};
    DrawLineEx(pos, end, 2, get_laser_color(laser_idx, is_active ? 128 : 64));

    // Draw elevation angle if active
    if (is_active)
    {
        DrawText(TextFormat("El: %.1f°", laser->el),
                 pos.x + 40, pos.y - 40 - laser_idx * 20,
                 15, text_color);
    }
}

// Add helper function to draw threat bracket
void draw_threat_bracket(Vector2 pos, Color color)
{
    float size = 25.0f;
    float thickness = 2.0f;

    // Draw selection bracket corners
    DrawLineEx((Vector2){pos.x - size, pos.y - size},
               (Vector2){pos.x - size / 2, pos.y - size}, thickness, color);
    DrawLineEx((Vector2){pos.x - size, pos.y - size},
               (Vector2){pos.x - size, pos.y - size / 2}, thickness, color);

    DrawLineEx((Vector2){pos.x + size, pos.y - size},
               (Vector2){pos.x + size / 2, pos.y - size}, thickness, color);
    DrawLineEx((Vector2){pos.x + size, pos.y - size},
               (Vector2){pos.x + size, pos.y - size / 2}, thickness, color);

    DrawLineEx((Vector2){pos.x - size, pos.y + size},
               (Vector2){pos.x - size / 2, pos.y + size}, thickness, color);
    DrawLineEx((Vector2){pos.x - size, pos.y + size},
               (Vector2){pos.x - size, pos.y + size / 2}, thickness, color);

    DrawLineEx((Vector2){pos.x + size, pos.y + size},
               (Vector2){pos.x + size / 2, pos.y + size}, thickness, color);
    DrawLineEx((Vector2){pos.x + size, pos.y + size},
               (Vector2){pos.x + size, pos.y + size / 2}, thickness, color);
}

// Improved seeker drawing with FOV arcs and terminal guidance visualization
void draw_seekers(Camera2D *cam, AirSim *sim, bool show_paths, int selected_seeker)
{
    for (int i = 0; i < MAX_SEEKERS; i++)
    {
        if (!sim->seekers[i].active)
            continue;

        Seeker *seeker = &sim->seekers[i];

        // Get screen position and calculate distance
        Vector2 pos = GetWorldToScreen2D((Vector2){
                                             seeker->x - sim->aircraft.x,
                                             seeker->y - sim->aircraft.y},
                                         *cam);

        float dist = compute_distance(
            seeker->x, seeker->y, seeker->z,
            sim->aircraft.x, sim->aircraft.y, sim->aircraft.z);

        // Draw heading triangle
        float heading = atan2f(seeker->vy, seeker->vx) * RAD2DEG;
        float size = 15.0f;
        Vector2 dir = {
            cosf(heading * DEG2RAD) * size,
            sinf(heading * DEG2RAD) * size};
        Vector2 v1 = {pos.x + dir.x, pos.y + dir.y};
        Vector2 v2 = {pos.x - dir.x / 2 - dir.y / 2, pos.y - dir.y / 2 + dir.x / 2};
        Vector2 v3 = {pos.x - dir.x / 2 + dir.y / 2, pos.y - dir.y / 2 - dir.x / 2};

        float engagement_range = 2000.0f * cam->zoom; // Arbitrary engagement range for visualization

        // Draw terminal guidance circle if within range
        float terminal_range = 50.0f;
        if (dist < terminal_range)
        {
            Color terminal_color = (Color){255, 0, 0, 64};
            DrawCircle(pos.x, pos.y, terminal_range * cam->zoom, terminal_color);
            DrawCircleLines(pos.x, pos.y, terminal_range * cam->zoom, RED);
        }
        else
        {
            // draw FOV arc
            float nav_alpha = (seeker->navigation_constant / sim->config.seeker_types[seeker->type].navigation_constant) * 64;
            Color fov_fill = (Color){255, 0, 0, (unsigned char)nav_alpha};
            Color fov_line = (Color){255, 0, 0, 128};

            DrawCircleSector(pos, engagement_range * 0.2f,
                             heading - seeker->fov,
                             heading + seeker->fov,
                             32, fov_fill);
            DrawCircleSectorLines(pos, engagement_range * 0.2f,
                                  heading - seeker->fov,
                                  heading + seeker->fov,
                                  32, fov_line);
        }

        DrawTriangle(v1, v2, v3, RED);
        DrawTriangleLines(v1, v2, v3, MAROON);

        if (i == selected_seeker)
        {
            draw_threat_bracket(pos, MAROON);
        }

        // Draw info text
        DrawText(TextFormat("#%d  %.0fm  %.0fz", i + 1, dist, seeker->z),
                 pos.x + 20, pos.y - 20, 20, BLACK);
        DrawText(TextFormat("%.1fs", seeker->lifetime - seeker->elapsed_time),
                 pos.x + 20, pos.y + 5, 20, BLACK);
    }
}

// Update main to draw aircraft as a triangle
void draw_aircraft(Camera2D *cam, AirSim *sim)
{
    // Get screen position of aircraft's actual position
    Vector2 pos = GetWorldToScreen2D((Vector2){sim->aircraft.x, sim->aircraft.y}, *cam);

    float heading = atan2f(sim->aircraft.vy, sim->aircraft.vx) * RAD2DEG;
    float size = 20.0f;

    Vector2 dir = {
        cosf(heading * DEG2RAD) * size,
        sinf(heading * DEG2RAD) * size};

    Vector2 v1 = {pos.x + dir.x, pos.y + dir.y};
    Vector2 v2 = {pos.x - dir.x / 2 - dir.y / 2, pos.y - dir.y / 2 + dir.x / 2};
    Vector2 v3 = {pos.x - dir.x / 2 + dir.y / 2, pos.y - dir.y / 2 - dir.x / 2};

    DrawTriangle(v1, v2, v3, BLUE);
    DrawTriangleLines(v1, v2, v3, DARKBLUE);
}

// Main simulation and visualization loop
int main(int argc, char **argv)
{
    // Load configuration (default "example.yaml", or use argv[1])
    const char *config_file = "example.yaml";
    if (argc > 1)
        config_file = argv[1];
    SimConfig *config = load_config(config_file);
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
                printf("Starting recording...\n");
                ffmpeg_handle = ffmpeg_start_rendering(WINDOW_WIDTH, WINDOW_HEIGHT, 30, NULL);
                if (ffmpeg_handle)
                    state.recording = true;
            }
            else
            {
                if (ffmpeg_handle)
                {
                    printf("Stopping recording...\n");
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
        // ENTER - Toggle Laser engagement through actions array
        if (IsKeyPressed(KEY_ENTER))
        {
            printf("Attempting to toggle laser %d engagement with seeker %d\n", state.active_laser, state.selected_threat);
            if (sim.seekers[state.selected_threat].active)
            {
                // If laser is already targeting this seeker, disengage
                if (sim.actions[state.active_laser] == state.selected_threat)
                {
                    printf("Disengaging laser %d from seeker %d\n", state.active_laser, state.selected_threat);
                    sim.actions[state.active_laser] = -1;
                }
                else
                {
                    // Otherwise, engage the seeker
                    printf("Engaging seeker %d with laser %d through actions\n", state.selected_threat, state.active_laser);
                    sim.actions[state.active_laser] = state.selected_threat;
                }
            }
        }

        // BACKSPACE - Disengage Laser through actions array
        if (IsKeyPressed(KEY_BACKSPACE))
        {
            printf("Disengaging laser %d through actions\n", state.active_laser);
            sim.actions[state.active_laser] = -1;
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
#ifdef DEBUG_PRINT
            print_debug_state(&sim); // Add debug printing
#endif
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

        // Replace simple circle aircraft with triangle
        draw_aircraft(&state.camera, &sim);

        // Draw seekers with improved visuals
        draw_seekers(&state.camera, &sim, state.show_threat_paths, state.selected_threat);

        // Draw lasers with improved visuals
        for (int i = 0; i < MAX_LASERS; i++)
        {
            if (sim.lasers[i].type > 0 && state.laser_view[i])
            {
                draw_laser_arc(&state.camera, &sim, i, i == state.active_laser);
            }
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

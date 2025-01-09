#include "airsim.h"
#include <stdio.h>

// Window/display settings
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define GRID_SIZE 100.0f  // Size of each grid square in meters
#define PIXELS_PER_METER 0.2f // Scale for rendering
#define RANGE_TEXT_SIZE 30

// Add laser color definitions
#define MAX_COLORS 5
const Color LASER_COLORS[MAX_COLORS] = {
    (Color){0, 128, 255, 255},    // Bright blue
    (Color){128, 0, 255, 255},    // Purple
    (Color){0, 64, 196, 255},     // Dark blue
    (Color){128, 0, 196, 255},    // Dark purple
    (Color){64, 64, 255, 255}     // Medium blue
};

// Add HP bar settings at top with other constants
#define HP_BAR_WIDTH 60.0f
#define HP_BAR_HEIGHT 8.0f
#define HP_BAR_PADDING 5.0f

// Helper to get laser color with custom alpha
Color get_laser_color(int laser_idx, unsigned char alpha) {
    Color c = LASER_COLORS[laser_idx];
    c.a = alpha;
    return c;
}

typedef struct Camera2DEx {
    Camera2D cam;
    float zoom_target;
    Vector2 offset_target;
} Camera2DEx;

typedef struct {
    bool paused;
    bool show_debug;
    bool show_threat_paths;  // Add new toggle for threat paths
    bool show_laser[MAX_LASERS];  // Add laser visibility toggles
    int active_laser;      // Currently selected laser
    int selected_threat;   // Currently selected threat
    bool show_help;    // Add help visibility toggle
    Camera2DEx camera;
} GameState;

// Convert simulation coordinates to screen coordinates
Vector2 world_to_screen(Camera2D* cam, float x, float y) {
    Vector2 world = {x, y};
    return GetWorldToScreen2D(world, *cam);
}

// Add new function to draw threat selection bracket
void draw_threat_bracket(Camera2D* cam, Vector2 pos, bool is_selected) {
    float size = 25.0f;
    float thickness = 4.0f;
    Color color = is_selected ? BLACK : (Color){128, 128, 128, 128};
    
    // Draw selection bracket
    DrawLineEx((Vector2){pos.x - size, pos.y - size}, 
               (Vector2){pos.x - size/2, pos.y - size}, thickness, color);
    DrawLineEx((Vector2){pos.x - size, pos.y - size}, 
               (Vector2){pos.x - size, pos.y - size/2}, thickness, color);
               
    DrawLineEx((Vector2){pos.x + size, pos.y - size}, 
               (Vector2){pos.x + size/2, pos.y - size}, thickness, color);
    DrawLineEx((Vector2){pos.x + size, pos.y - size}, 
               (Vector2){pos.x + size, pos.y - size/2}, thickness, color);
               
    DrawLineEx((Vector2){pos.x - size, pos.y + size}, 
               (Vector2){pos.x - size/2, pos.y + size}, thickness, color);
    DrawLineEx((Vector2){pos.x - size, pos.y + size}, 
               (Vector2){pos.x - size, pos.y + size/2}, thickness, color);
               
    DrawLineEx((Vector2){pos.x + size, pos.y + size}, 
               (Vector2){pos.x + size/2, pos.y + size}, thickness, color);
    DrawLineEx((Vector2){pos.x + size, pos.y + size}, 
               (Vector2){pos.x + size, pos.y + size/2}, thickness, color);
}

void draw_laser_arc(Camera2D* cam, AirSim* sim, int laser_idx, bool is_active) {
    Vector2 pos = world_to_screen(cam, sim->aircraft_x, sim->aircraft_y);
    Laser* laser = &sim->lasers[laser_idx];
    
    // Convert angles to radians
    float az_rad = laser->az * PI / 180.0f;
    float fov_rad = laser->fov * PI / 180.0f;
    float range = laser->maximum_range * cam->zoom;
    
    // Draw laser arc
    float start_angle = az_rad - fov_rad;
    float end_angle = az_rad + fov_rad;
    
    // Get base color for this laser
    Color fill_color = get_laser_color(laser_idx, is_active ? 32 : 8);
    Color line_color = get_laser_color(laser_idx, is_active ? 255 : 64);
    Color text_color = get_laser_color(laser_idx, is_active ? 255 : 64);
    
    // Draw arc fill with transparency
    DrawCircleSector(pos, range, 
                    start_angle * RAD2DEG, 
                    end_angle * RAD2DEG, 
                    32, fill_color);
                    
    // Draw arc outline
    DrawCircleSectorLines(pos, range, 
                         start_angle * RAD2DEG, 
                         end_angle * RAD2DEG, 
                         32, line_color);
                         
    // Draw center line showing azimuth
    Vector2 end = {
        pos.x + cosf(az_rad) * range,
        pos.y + sinf(az_rad) * range
    };
    DrawLineEx(pos, end, 2, get_laser_color(laser_idx, is_active ? 128 : 32));
    
    if (is_active) {
        DrawText(TextFormat("El: %.1f°", laser->el),
                pos.x + 40, pos.y - 40 - laser_idx * 20, 
                15, text_color);
    }
}

void draw_aircraft(Camera2D* cam, AirSim* sim, GameState* state) {
    Vector2 pos = world_to_screen(cam, sim->aircraft_x, sim->aircraft_y);
    
    float size = 30.0f;
    
    // Draw triangle relative to screen position
    Vector2 v1 = {pos.x + size, pos.y};
    Vector2 v2 = {pos.x - size/2, pos.y + size/2};
    Vector2 v3 = {pos.x - size/2, pos.y - size/2};
    
    DrawTriangle(v1, v2, v3, BLACK);
    
    // Draw outline for better visibility with slightly lighter color
    DrawTriangleLines(v1, v2, v3, GRAY);
    
    // Debug text showing position
    if (IsKeyDown(KEY_TAB)) {
        DrawText(TextFormat("Aircraft: (%.0f, %.0f)", pos.x, pos.y), 
                pos.x + size, pos.y - size, 20, BLACK);
    }

    // Draw active laser arcs
    for (int i = 0; i < MAX_LASERS; i++) {
        if (state->show_laser[i] && sim->lasers[i].type > 0) {
            draw_laser_arc(cam, sim, i, i == state->active_laser);
        }
    }
}

void draw_threats(Camera2D* cam, AirSim* sim, bool show_paths, int selected_threat) {
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0) {
            Vector2 pos = world_to_screen(cam, sim->threats[i].x, sim->threats[i].y);
            
            // Draw threat dot sized to lethal radius
            float lethal_radius = sim->threats[i].lethal_radius * cam->zoom;
            DrawCircleV(pos, lethal_radius, RED);
            
            // Only draw engagement radius for selected threat
            if (i == selected_threat) {
                float radius = sim->threats[i].engagement_radius * cam->zoom;
                DrawCircleLines(pos.x, pos.y, radius, RED);
            }
            
            // Draw range text
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z
            );
            DrawText(TextFormat("%.0fm", dist), pos.x + 10, pos.y - 15, RANGE_TEXT_SIZE, BLACK);
            
            // Draw HP bar above range text
            Vector2 bar_pos = {pos.x + 10, pos.y - 15 - HP_BAR_HEIGHT - HP_BAR_PADDING};
            float hp_fraction = sim->threats[i].hp / 100.0f;  // Assuming max HP is 100
            
            // Bar background
            DrawRectangle(bar_pos.x, bar_pos.y, HP_BAR_WIDTH, HP_BAR_HEIGHT, GRAY);
            // HP remaining
            DrawRectangle(bar_pos.x, bar_pos.y, 
                         HP_BAR_WIDTH * hp_fraction, HP_BAR_HEIGHT, 
                         RED);
            // Bar outline
            DrawRectangleLines(bar_pos.x, bar_pos.y, HP_BAR_WIDTH, HP_BAR_HEIGHT, BLACK);

            // Draw dotted line path to aircraft if enabled
            if (show_paths) {
                Vector2 aircraft_pos = world_to_screen(cam, sim->aircraft_x, sim->aircraft_y);
                float dx = aircraft_pos.x - pos.x;
                float dy = aircraft_pos.y - pos.y;
                float len = sqrtf(dx*dx + dy*dy);
                if (len > 0) {
                    dx /= len;
                    dy /= len;
                    for (float d = 0; d < len; d += 20.0f) {
                        if ((int)(d/20.0f) % 2 == 0) {  // Draw every other segment
                            DrawLineEx(
                                (Vector2){pos.x + dx*d, pos.y + dy*d},
                                (Vector2){pos.x + dx*(d+10.0f), pos.y + dy*(d+10.0f)},
                                2,
                                RED
                            );
                        }
                    }
                }
            }
            
            // Draw selection bracket if this is the selected threat
            if (i == selected_threat) {
                draw_threat_bracket(cam, pos, true);
            }
        }
    }
}

void draw_grid(Camera2D* cam, float grid_size) {
    Vector2 screen_center = GetScreenToWorld2D((Vector2){WINDOW_WIDTH/2, WINDOW_HEIGHT/2}, *cam);
    int grid_cells = 50;
    
    float start_x = screen_center.x - (grid_cells/2) * grid_size;
    float start_y = screen_center.y - (grid_cells/2) * grid_size;
    
    for (int i = 0; i <= grid_cells; i++) {
        Vector2 v1 = world_to_screen(cam, start_x + i*grid_size, start_y);
        Vector2 v2 = world_to_screen(cam, start_x + i*grid_size, start_y + grid_cells*grid_size);
        Vector2 h1 = world_to_screen(cam, start_x, start_y + i*grid_size);
        Vector2 h2 = world_to_screen(cam, start_x + grid_cells*grid_size, start_y + i*grid_size);
        
        DrawLineV(v1, v2, (Color){200, 200, 200, 64});
        DrawLineV(h1, h2, (Color){200, 200, 200, 64});
    }
}

// Add new function to draw help panel
void draw_help_panel(void) {
    const char* help_text[] = {
        "F1 - Toggle Help",
        "SPACE - Pause/Resume",
        "R - Reset Simulation",
        "P - Toggle Threat Paths",
        "TAB - Debug Info",
        "MMB / Wheel /  - Pan / Zoom Camera",
        "LEFT/RIGHT→ - Select Threat",
        "UP/DOWN - Select Laser",  // Added new control hint
        "ENTER/BACKSPACE - Engage/Disengage Laser",
        "1-5 - Toggle Laser View",
        "CLICK - Step Forward",  // Added new control hint
        "ESC - Quit"
    };
    
    int num_lines = sizeof(help_text) / sizeof(help_text[0]);
    int line_height = 25;
    int padding = 10;
    int width = 250;
    int height = num_lines * line_height + 2 * padding;
    
    // Draw semi-transparent background
    DrawRectangle(WINDOW_WIDTH - width - padding, padding, 
                 width, height, 
                 (Color){255, 255, 255, 230});
    
    // Draw help text
    for (int i = 0; i < num_lines; i++) {
        DrawText(help_text[i], 
                WINDOW_WIDTH - width, 
                padding + i * line_height, 
                20, DARKGRAY);
    }
}

void draw_hud(AirSim* sim, GameState* state) {
    // Top HUD - Aircraft info and sim status
    DrawRectangle(0, 0, WINDOW_WIDTH, 60, (Color){255, 255, 255, 200});
    
    // Draw F1 help hint always
    DrawText("F1 - Toggle Help", WINDOW_WIDTH - 150, 10, 20, DARKGRAY);
    
    DrawText(TextFormat("Aircraft Pos: (%.0f, %.0f, %.0f)", 
        sim->aircraft_x, sim->aircraft_y, sim->aircraft_z), 10, 10, 20, BLACK);
    DrawText(TextFormat("Step: %d Time: %.1fs %s %s", 
        sim->steps, sim->time, 
        state->paused ? "PAUSED" : "",
        sim->terminal ? "TERMINAL" : ""), 
        10, 35, 20, sim->terminal ? RED : BLACK);

    // Draw help panel if enabled
    if (state->show_help) {
        draw_help_panel();
    }
    
    // Bottom HUD - Controls and status
    DrawRectangle(0, WINDOW_HEIGHT - 90, WINDOW_WIDTH, 90, (Color){255, 255, 255, 200});
    
    // First line - Status only - now reading engagement from sim directly
    bool is_engaged = (sim->lasers[state->active_laser].engaging_threat >= 0);
    Color status_color = is_engaged ? LASER_COLORS[state->active_laser] : DARKGRAY;
    DrawText(TextFormat("Active Laser: L%d  Selected Threat: %d  %s", 
        state->active_laser + 1, 
        state->selected_threat + 1,
        is_engaged ? "ENGAGED" : ""),
        10, WINDOW_HEIGHT - 80, 20, status_color);
    
    // Second line - Laser visibility toggles
    DrawText("Laser Status: ", 10, WINDOW_HEIGHT - 45, 20, DARKGRAY);
    for (int i = 0; i < MAX_LASERS; i++) {
        if (sim->lasers[i].type > 0) {
            Color color = state->show_laser[i] ? LASER_COLORS[i] : DARKGRAY;
            DrawText(TextFormat("L%d", i+1), 
                    150 + i*40, WINDOW_HEIGHT - 45, 20, color);
        }
    }

    // Terminal state indicator in center screen
    if (sim->terminal) {
        const char* text = "TERMINAL STATE";
        int fontSize = 40;
        int textWidth = MeasureText(text, fontSize);
        DrawText(text, 
            (WINDOW_WIDTH - textWidth)/2,
            WINDOW_HEIGHT/2 - fontSize/2,
            fontSize, RED);
    }
}

void update_camera(Camera2DEx* camera, AirSim* sim) {
    // Add panning with middle mouse button
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        camera->cam.offset.x += delta.x;
        camera->cam.offset.y += delta.y;
        camera->offset_target = camera->cam.offset;
    } else {
        // Center aircraft on screen
        camera->offset_target = (Vector2){
            WINDOW_WIDTH/2,  // Center x
            WINDOW_HEIGHT/2  // Center y
        };
        
        camera->cam.target = (Vector2){
            sim->aircraft_x,
            sim->aircraft_y
        };
        
        //camera->cam.offset.x += (camera->offset_target.x - camera->cam.offset.x) * 0.1f;
        //camera->cam.offset.y += (camera->offset_target.y - camera->cam.offset.y) * 0.1f;
    }
    
    // Handle zoom with mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        camera->zoom_target *= (1.0f + wheel * 0.1f);
        camera->zoom_target = fmaxf(0.1f, fminf(camera->zoom_target, 10.0f));
    }
    
    camera->cam.zoom += (camera->zoom_target - camera->cam.zoom) * 0.1f;
}

// Add helper function to find next/prev active threat
int find_next_active_threat(AirSim* sim, int current, bool forward) {
    for (int i = 0; i < MAX_THREATS; i++) {
        int idx = forward ? 
            (current + 1 + i) % MAX_THREATS : 
            (current - 1 - i + MAX_THREATS) % MAX_THREATS;
        if (sim->threats[idx].type > 0) {
            return idx;
        }
    }
    return current; // Keep current if no other active threats found
}

// Add function to reset UI state
void reset_ui_state(GameState* state) {
    state->paused = true;
    state->show_debug = false;
    state->show_threat_paths = true;
    state->selected_threat = 0;
    
    // Keep only first laser visible
    for (int i = 0; i < MAX_LASERS; i++) {
        state->show_laser[i] = (i == 0);
    }
    state->active_laser = 0;
    
    // Don't reset help visibility
    // state->show_help = false;
    
    // Reset camera zoom but keep position
    state->camera.zoom_target = PIXELS_PER_METER * 4.0f;
    state->camera.cam.zoom = PIXELS_PER_METER * 4.0f;
}

int main() {
    // Initialize simulation
    AirSim sim = {
        .initial_distance = 1000.0f,  // Reduced from 2000.0f
        .aircraft_speed = 100.0f,
        .threat_acceleration = 400.0f,
        .threat_max_velocity = 1000.0f,
        .engagement_radius = 2500.0f,
        .dt = 0.016f,          // ~60 FPS
        .max_time = 60.0f,
        .max_steps = 6000,
    };
    allocate(&sim);

    // Initialize window and renderer
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "AirSim Visualizer");
    SetTargetFPS(60);

    GameState state = {
        .paused = true,
        .show_debug = false,
        .show_threat_paths = true, 
        .show_laser = {true, false, false, false, false},  // Start with first laser visible
        .active_laser = 0,
        .selected_threat = 0,
        .show_help = false,  // Start with help hidden
        .camera = {
            .cam = {
                .zoom = PIXELS_PER_METER * 4.0f, // don't change this
                .offset = {WINDOW_WIDTH/2, WINDOW_HEIGHT/2},
                .rotation = 0.0f,
                .target = {0, 0}
            },
            .zoom_target = PIXELS_PER_METER * 4.0f,
        }
    };

    reset(&sim);

    while (!WindowShouldClose()) {
        // Input handling
        if (IsKeyPressed(KEY_F1)) state.show_help = !state.show_help;
        if (IsKeyPressed(KEY_SPACE)) state.paused = !state.paused;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { // Mouse click to step
            step(&sim);
        }
        if (IsKeyPressed(KEY_R)) {
            reset(&sim);
            reset_ui_state(&state);
        }
        if (IsKeyPressed(KEY_TAB)) state.show_debug = !state.show_debug;
        if (IsKeyPressed(KEY_P)) state.show_threat_paths = !state.show_threat_paths;  // Add 'P' key toggle
        
        // Handle laser toggles (keys 1-5)
        for (int i = 0; i < MAX_LASERS; i++) {
            if (IsKeyPressed(KEY_ONE + i)) {
                state.show_laser[i] = !state.show_laser[i];
            }
        }
        
        // Handle laser selection with up/down keys
        if (IsKeyPressed(KEY_UP)) {
            state.active_laser = (state.active_laser - 1 + MAX_LASERS) % MAX_LASERS;
            // Only select active lasers
            while (sim.lasers[state.active_laser].type == 0) {
                state.active_laser = (state.active_laser - 1 + MAX_LASERS) % MAX_LASERS;
            }
        }
        if (IsKeyPressed(KEY_DOWN)) {
            state.active_laser = (state.active_laser + 1) % MAX_LASERS;
            // Only select active lasers
            while (sim.lasers[state.active_laser].type == 0) {
                state.active_laser = (state.active_laser + 1) % MAX_LASERS;
            }
        }
        
        // Handle threat selection with arrow keys - only cycle through active threats
        if (IsKeyPressed(KEY_RIGHT)) {
            state.selected_threat = find_next_active_threat(&sim, state.selected_threat, true);
        }
        if (IsKeyPressed(KEY_LEFT)) {
            state.selected_threat = find_next_active_threat(&sim, state.selected_threat, false);
        }
        
        // Handle laser engagement (ENTER to engage, BACKSPACE to disengage)
        if (IsKeyPressed(KEY_ENTER)) {
            if (sim.threats[state.selected_threat].type > 0) {
                printf("[Step %d] Keyboard command to Laser %d engaging threat %d\n", sim.steps, state.active_laser, state.selected_threat);
                sim.lasers[state.active_laser].engaging_threat = state.selected_threat;
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            sim.lasers[state.active_laser].engaging_threat = -1;
        }
        
        // Update simulation if not paused and not terminal
        if (!state.paused && !sim.terminal) {
            step(&sim);
        }
        
        // Pause simulation when terminal state is reached
        if (sim.terminal) {
            state.paused = true;
        }

        // Update camera
        update_camera(&state.camera, &sim);

        // Render
        BeginDrawing();
        ClearBackground(WHITE);  // Changed from BLACK
        
        BeginMode2D(state.camera.cam);
        draw_grid(&state.camera.cam, GRID_SIZE);
        draw_aircraft(&state.camera.cam, &sim, &state);  // Pass state to draw_aircraft
        draw_threats(&state.camera.cam, &sim, state.show_threat_paths, state.selected_threat);  // Pass show_paths parameter
        EndMode2D();
        
        draw_hud(&sim, &state);
        EndDrawing();
    }

    free_allocated(&sim);
    CloseWindow();
    return 0;
}

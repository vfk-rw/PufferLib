#include "airsim.h"
#include <stdio.h>

// Window/display settings
#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define GRID_SIZE 100.0f  // Size of each grid square in meters
#define PIXELS_PER_METER 0.1f // Scale for rendering

typedef struct Camera2DEx {
    Camera2D cam;
    float zoom_target;
    Vector2 offset_target;
} Camera2DEx;

typedef struct {
    bool paused;
    bool show_debug;
    bool show_threat_paths;  // Add new toggle for threat paths
    Camera2DEx camera;
} GameState;

// Convert simulation coordinates to screen coordinates
Vector2 world_to_screen(Camera2D* cam, float x, float y) {
    Vector2 world = {x, y};
    return GetWorldToScreen2D(world, *cam);
}

void draw_aircraft(Camera2D* cam, AirSim* sim) {
    Vector2 pos = world_to_screen(cam, sim->aircraft_x, sim->aircraft_y);
    
    // Draw larger blue triangle pointing in x direction
    float size = 30.0f;  // Reduced size to be more reasonable
    
    // Draw a red dot at aircraft position for debugging
    DrawCircleV(pos, 5.0f, RED);
    
    // Draw triangle relative to screen position
    Vector2 v1 = {pos.x + size, pos.y};
    Vector2 v2 = {pos.x - size/2, pos.y + size/2};
    Vector2 v3 = {pos.x - size/2, pos.y - size/2};
    
    DrawTriangle(v1, v2, v3, DARKBLUE);
    
    // Draw outline for better visibility
    DrawTriangleLines(v1, v2, v3, BLUE);
    
    // Debug text showing position
    if (IsKeyDown(KEY_TAB)) {
        DrawText(TextFormat("Aircraft: (%.0f, %.0f)", pos.x, pos.y), 
                pos.x + size, pos.y - size, 20, BLACK);
    }
}

void draw_threats(Camera2D* cam, AirSim* sim, bool show_paths) {
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0) {
            Vector2 pos = world_to_screen(cam, sim->threats[i].x, sim->threats[i].y);
            
            // Draw threat dot
            DrawCircleV(pos, 5.0f, RED);
            
            // Draw range to aircraft with larger text
            float dist = compute_distance(
                sim->threats[i].x, sim->threats[i].y, sim->threats[i].z,
                sim->aircraft_x, sim->aircraft_y, sim->aircraft_z
            );
            DrawText(TextFormat("%.0fm", dist), pos.x + 10, pos.y - 15, 25, BLACK);  // Increased size to 25
            
            // Draw engagement radius
            float radius = sim->threats[i].engagement_radius * cam->zoom;
            DrawCircleLines(pos.x, pos.y, radius, RED);

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

void draw_hud(AirSim* sim, GameState* state) {
    DrawRectangle(0, 0, WINDOW_WIDTH, 60, (Color){255, 255, 255, 200});
    
    DrawText(TextFormat("Aircraft Pos: (%.0f, %.0f, %.0f)", 
        sim->aircraft_x, sim->aircraft_y, sim->aircraft_z), 10, 10, 20, BLACK);
    DrawText(TextFormat("Step: %d Time: %.1fs %s %s", 
        sim->steps, sim->time, 
        state->paused ? "PAUSED" : "",
        sim->terminal ? "TERMINAL" : ""), 
        10, 35, 20, sim->terminal ? RED : BLACK);
        
    // Draw large terminal indicator in center of screen when terminal
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
        // Smoothly track aircraft
        camera->offset_target = (Vector2){
            WINDOW_WIDTH/2 - sim->aircraft_x * camera->cam.zoom,
            WINDOW_HEIGHT/2 - sim->aircraft_y * camera->cam.zoom
        };
        
        camera->cam.offset.x += (camera->offset_target.x - camera->cam.offset.x) * 0.1f;
        camera->cam.offset.y += (camera->offset_target.y - camera->cam.offset.y) * 0.1f;
    }
    
    // Handle zoom with mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        camera->zoom_target *= (1.0f + wheel * 0.1f);
        camera->zoom_target = fmaxf(0.1f, fminf(camera->zoom_target, 10.0f));
    }
    
    camera->cam.zoom += (camera->zoom_target - camera->cam.zoom) * 0.1f;
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
        .show_threat_paths = false,  // Initialize new toggle
        .camera = {
            .cam = {
                .zoom = PIXELS_PER_METER * 0.5f,  // Start more zoomed out to see everything
                .offset = {WINDOW_WIDTH/2, WINDOW_HEIGHT/2},
                .rotation = 0.0f,
                .target = {0, 0}
            },
            .zoom_target = PIXELS_PER_METER * 0.5f,
        }
    };

    reset(&sim);

    while (!WindowShouldClose()) {
        // Input handling
        if (IsKeyPressed(KEY_SPACE)) state.paused = !state.paused;
        if (IsKeyPressed(KEY_R)) reset(&sim);
        if (IsKeyPressed(KEY_TAB)) state.show_debug = !state.show_debug;
        if (IsKeyPressed(KEY_P)) state.show_threat_paths = !state.show_threat_paths;  // Add 'P' key toggle
        
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
        draw_aircraft(&state.camera.cam, &sim);
        draw_threats(&state.camera.cam, &sim, state.show_threat_paths);  // Pass show_paths parameter
        EndMode2D();
        
        draw_hud(&sim, &state);
        EndDrawing();
    }

    free_allocated(&sim);
    CloseWindow();
    return 0;
}

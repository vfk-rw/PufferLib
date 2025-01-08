#include <stdio.h>
#include "airsim.h"
#include "raylib.h"

#define VIEW_RADIUS 3000.0f  // How much of the grid to show around aircraft
#define GRID_SPACING 500.0f  // Space between grid lines
#define AIRCRAFT_SIZE 40.0f  // Size of aircraft triangle
#define MIN_ZOOM 0.1f
#define MAX_ZOOM 2.0f
#define ZOOM_SPEED 0.1f

// Adjust camera zoom calculation for proper scaling
void setup_camera(Camera2D* camera) {
    camera->offset = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight()/2.0f };
    camera->rotation = 0.0f;
    camera->zoom = 1.0f;  // Initialize zoom to default
}

// New function to handle zoom with keyboard (e.g., Z to zoom in, X to zoom out)
void handle_zoom(Camera2D* camera) {
    if (IsKeyDown(KEY_Z)) {
        camera->zoom += ZOOM_SPEED * GetFrameTime();
        camera->zoom = Clamp(camera->zoom, MIN_ZOOM, MAX_ZOOM);
    }
    if (IsKeyDown(KEY_X)) {
        camera->zoom -= ZOOM_SPEED * GetFrameTime();
        camera->zoom = Clamp(camera->zoom, MIN_ZOOM, MAX_ZOOM);
    }
}

// Convert world coordinates to screen coordinates
Vector2 world_to_screen(Camera2D* camera, Vector2 world_pos) {
    Vector2 screen_pos = GetScreenToWorld2D(world_pos, *camera);
    return screen_pos;
}

void draw_grid(Camera2D* camera, Vector2 aircraft_pos) {
    // Draw grid lines
    for (float x = -VIEW_RADIUS; x <= VIEW_RADIUS; x += GRID_SPACING) {
        Vector2 start = world_to_screen(camera, (Vector2){aircraft_pos.x + x, aircraft_pos.y - VIEW_RADIUS});
        Vector2 end = world_to_screen(camera, (Vector2){aircraft_pos.x + x, aircraft_pos.y + VIEW_RADIUS});
        DrawLineV(start, end, LIGHTGRAY);
        
        // Draw coordinate labels every 1000m
        if (fmodf(x, 1000.0f) == 0) {
            char text[32];
            sprintf(text, "%.0fm", x);
            DrawText(text, start.x + 5, camera->offset.y + 5, 10, DARKGRAY);
        }
    }
    
    for (float y = -VIEW_RADIUS; y <= VIEW_RADIUS; y += GRID_SPACING) {
        Vector2 start = world_to_screen(camera, (Vector2){aircraft_pos.x - VIEW_RADIUS, aircraft_pos.y + y});
        Vector2 end = world_to_screen(camera, (Vector2){aircraft_pos.x + VIEW_RADIUS, aircraft_pos.y + y});
        DrawLineV(start, end, LIGHTGRAY);
        
        if (fmodf(y, 1000.0f) == 0) {
            char text[32];
            sprintf(text, "%.0fm", y);
            DrawText(text, camera->offset.x + 5, start.y + 5, 10, DARKGRAY);
        }
    }
}

void draw_aircraft(Camera2D* camera) {
    // Aircraft is always at screen center, pointing right
    Vector2 center = (Vector2){camera->offset.x, camera->offset.y};
    Vector2 nose = (Vector2){center.x + AIRCRAFT_SIZE, center.y};
    Vector2 tail_left = (Vector2){center.x - AIRCRAFT_SIZE/2, center.y - AIRCRAFT_SIZE/2};
    Vector2 tail_right = (Vector2){center.x - AIRCRAFT_SIZE/2, center.y + AIRCRAFT_SIZE/2};
    
    DrawTriangle(nose, tail_left, tail_right, BLACK);
}

// New function to draw arrow pointing to off-grid threat
void draw_off_grid_indicator(Camera2D* camera, Vector2 threat_pos, Vector2 aircraft_pos, Color color) {
    // Get screen bounds
    Vector2 screen_center = (Vector2){camera->offset.x, camera->offset.y};
    float screen_radius = fminf(GetScreenWidth(), GetScreenHeight()) * 0.45f;
    
    // Calculate direction to threat
    Vector2 dir = {
        threat_pos.x - aircraft_pos.x,
        threat_pos.y - aircraft_pos.y
    };
    float dist = sqrtf(dir.x * dir.x + dir.y * dir.y);
    
    // Normalize direction
    dir.x /= dist;
    dir.y /= dist;
    
    // Calculate arrow position on screen edge
    Vector2 arrow_pos = {
        screen_center.x + dir.x * screen_radius,
        screen_center.y + dir.y * screen_radius
    };
    
    // Draw arrow
    float arrow_size = 20.0f;
    float angle = atan2f(dir.y, dir.x);
    
    // Arrow body
    DrawLineEx(arrow_pos, 
              (Vector2){arrow_pos.x - dir.x * arrow_size, 
                       arrow_pos.y - dir.y * arrow_size}, 
              3.0f, color);
    
    // Arrow head
    Vector2 left = {
        arrow_pos.x - arrow_size * cosf(angle + PI/6),
        arrow_pos.y - arrow_size * sinf(angle + PI/6)
    };
    Vector2 right = {
        arrow_pos.x - arrow_size * cosf(angle - PI/6),
        arrow_pos.y - arrow_size * sinf(angle - PI/6)
    };
    
    DrawLineEx(arrow_pos, left, 3.0f, color);
    DrawLineEx(arrow_pos, right, 3.0f, color);
    
    // Draw distance
    DrawText(TextFormat("%.0fm", dist),
            arrow_pos.x + dir.x * 25,
            arrow_pos.y + dir.y * 25,
            20, color);
}

void draw_threats(Camera2D* camera, AirSim* sim) {
    Vector2 aircraft_pos = {sim->aircraft_x, sim->aircraft_y};
    float view_bounds = VIEW_RADIUS / camera->zoom;
    
    for (int i = 0; i < MAX_THREATS; i++) {
        if (sim->threats[i].type > 0) {
            Vector2 threat_pos = {sim->threats[i].x, sim->threats[i].y};
            Color color = sim->threats[i].engaged ? RED : ORANGE;
            
            // Calculate distance
            float dx = threat_pos.x - aircraft_pos.x;
            float dy = threat_pos.y - aircraft_pos.y;
            float dist = sqrtf(dx*dx + dy*dy);
            
            if (dist <= view_bounds) {
                // Draw threat normally if in bounds
                Vector2 screen_pos = world_to_screen(camera, threat_pos);
                DrawCircleV(screen_pos, 5.0f, color);
                DrawText(TextFormat("T%d: %.0fm", i, dist), 
                        screen_pos.x + 10, screen_pos.y - 10, 20, color);
                
                // Draw targeting bracket if engaged
                for (int j = 0; j < MAX_LASERS; j++) {
                    if (sim->lasers[j].engaging_threat == i) {
                        DrawRectangleLines(
                            screen_pos.x - 10, screen_pos.y - 10,
                            20, 20, BLACK);
                        break;
                    }
                }
            } else {
                // Draw off-grid indicator for threats outside view
                draw_off_grid_indicator(camera, threat_pos, aircraft_pos, color);
            }
        }
    }
}

void draw_hud(AirSim* sim, bool paused) {
    int y = 10;
    DrawText(TextFormat("Time: %.2f", sim->time), 10, y, 20, BLACK); y += 25;
    DrawText(TextFormat("Aircraft: (%.0f, %.0f, %.0f)", 
            sim->aircraft_x, sim->aircraft_y, sim->aircraft_z), 10, y, 20, BLACK); y += 25;
    
    // Show laser status
    DrawText("Laser Status:", 10, y, 20, BLACK); y += 25;
    for (int i = 0; i < MAX_LASERS; i++) {
        const char* status = sim->lasers[i].engaging_threat >= 0 ? 
            TextFormat("Engaging T%d", sim->lasers[i].engaging_threat) : "No Target";
        DrawText(TextFormat("L%d: %s", i, status), 20, y, 20, BLACK);
        y += 20;
    }
    
    // Controls
    y += 10;
    DrawText("SPACE: Reset  |  P: Pause  |  LEFT/RIGHT: Target", 10, y, 20, BLACK);
    if (paused) {
        DrawText("PAUSED", GetScreenWidth()/2 - 50, 30, 30, RED);
    }
}

void render_scene(AirSim* sim, Camera2D* camera, bool paused) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    
    handle_zoom(camera);  // Handle zoom input each frame
    
    BeginMode2D(*camera);
    
    // Update camera to center on aircraft
    camera->target = (Vector2){sim->aircraft_x, sim->aircraft_y};
    
    draw_grid(camera, (Vector2){sim->aircraft_x, sim->aircraft_y});
    draw_aircraft(camera);
    draw_threats(camera, sim);
    
    EndMode2D();
    
    draw_hud(sim, paused);
    
    if (sim->terminal) {
        DrawText("SIMULATION ENDED", 
                GetScreenWidth()/2 - 100, 
                GetScreenHeight()/2, 
                30, RED);
    }
    
    EndDrawing();
}

int main() {
    InitWindow(1280, 720, "AirSim Test");
    SetTargetFPS(60);
    
    Camera2D camera = {0};
    setup_camera(&camera);
    
    // Initialize simulation
    AirSim* sim = (AirSim*)calloc(1, sizeof(AirSim));
    if (!sim) {
        printf("Failed to allocate simulation\n");
        return 1;
    }

    sim->initial_distance = 2000.0f;
    sim->aircraft_speed = 100.0f;
    sim->threat_acceleration = 400.0f;
    sim->threat_max_velocity = 1000.0f;
    sim->engagement_radius = 2500.0f;
    sim->dt = 0.016f;
    sim->max_time = 60.0f;
    sim->max_steps = 3600;

    allocate(sim);
    reset(sim);
    
    bool paused = false;

    while (!WindowShouldClose()) {
        // Input handling
        if (IsKeyPressed(KEY_SPACE)) reset(sim);
        if (IsKeyPressed(KEY_P)) paused = !paused;
        
        // Zoom controls
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            camera.zoom = Clamp(camera.zoom + wheel * ZOOM_SPEED, MIN_ZOOM, MAX_ZOOM);
        }
        
        if (!paused) {
            // Manual laser targeting
            sim->actions[0] = -1;
            if (IsKeyDown(KEY_LEFT)) sim->actions[0] = 0;
            if (IsKeyDown(KEY_RIGHT)) sim->actions[0] = 1;
            
            step(sim);
        }
        
        render_scene(sim, &camera, paused);
    }

    free_allocated(sim);
    free(sim);
    CloseWindow();
    return 0;
}

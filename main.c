#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <time.h>
#include "Application.h"

// Post-processing fragment shader: a cheap bloom (bright areas bleed glow) plus
// a subtle vignette. This is what makes lava, fire and water highlights pop.
static const char *BLOOM_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 resolution;\n"
    "void main() {\n"
    "    vec3 base = texture(texture0, fragTexCoord).rgb;\n"
    "    vec3 bloom = vec3(0.0);\n"
    "    for (int x = -3; x <= 3; x++) {\n"
    "        for (int y = -3; y <= 3; y++) {\n"
    "            vec2 off = vec2(float(x), float(y)) * 1.5 / resolution;\n"
    "            vec3 s = texture(texture0, fragTexCoord + off).rgb;\n"
    "            float b = max(s.r, max(s.g, s.b));\n"
    "            bloom += s * smoothstep(0.55, 1.0, b);\n"
    "        }\n"
    "    }\n"
    "    bloom /= 49.0;\n"
    "    vec3 col = base + bloom * 1.4;\n"
    "    vec2 uv = fragTexCoord - 0.5;\n"
    "    col *= mix(0.80, 1.0, smoothstep(0.95, 0.25, length(uv)));\n"
    "    finalColor = vec4(col, 1.0);\n"
    "}\n";

#define MIN_ZOOM 0.5f
#define MAX_ZOOM 6.0f

int main(void) {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Cellular Automata Sims By MFRSiam");
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(NULL));

    // The grid window must cover what's visible even fully zoomed out, plus a
    // margin so streaming has slack at the edges.
    int bufW = (int)(screenWidth  / (float)CELL_SIZE / MIN_ZOOM) + 16;
    int bufH = (int)(screenHeight / (float)CELL_SIZE / MIN_ZOOM) + 16;
    Grid grid = GridCreate(bufW * CELL_SIZE, bufH * CELL_SIZE);
    grid.cave.seed = (unsigned int)GetRandomValue(1, 1000000);

    Camera2D camera = {
        .target = {0, 0},
        .offset = {screenWidth / 2.0f, screenHeight / 2.0f},
        .rotation = 0.0f, .zoom = 1.0f,
    };

    // Center the window on the camera and build the initial world.
    grid.originX = (int)floorf(camera.target.x / CELL_SIZE) - grid.width / 2;
    grid.originY = (int)floorf(camera.target.y / CELL_SIZE) - grid.height / 2;
    GridRegenerate(&grid);

    RenderTexture2D target = LoadRenderTexture(screenWidth, screenHeight);
    Shader bloom = LoadShaderFromMemory(0, BLOOM_FS);
    float resolution[2] = {(float)screenWidth, (float)screenHeight};
    SetShaderValue(bloom, GetShaderLocation(bloom, "resolution"), resolution,
                   SHADER_UNIFORM_VEC2);

    Cell selected = CELL_SAND;
    int brushRadius = 3;

    const Cell palette[] = {
        CELL_SAND, CELL_WATER, CELL_WOOD, CELL_OIL, CELL_ACID,
        CELL_SNOW, CELL_FIRE, CELL_GAS, CELL_VAPOR,
    };
    const int paletteCount = sizeof(palette) / sizeof(palette[0]);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Material selection -----------------------------------------
        for (int k = 0; k < paletteCount; k++)
            if (IsKeyPressed(KEY_ONE + k)) selected = palette[k];
        if (IsKeyPressed(KEY_ZERO)) selected = CELL_EMPTY;
        if (IsKeyPressed(KEY_L))    selected = CELL_LAVA;
        if (IsKeyPressed(KEY_R))    selected = CELL_ROCK;
        if (IsKeyPressed(KEY_M))    selected = CELL_MUD;
        if (IsKeyPressed(KEY_C))    GridClear(&grid);

        // --- Camera: WASD / arrows pan, Q/E zoom, middle-drag pan -------
        float panSpeed = 500.0f * dt / camera.zoom;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    camera.target.y -= panSpeed;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  camera.target.y += panSpeed;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  camera.target.x -= panSpeed;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camera.target.x += panSpeed;
        if (IsKeyDown(KEY_E)) camera.zoom *= 1.0f + 1.5f * dt;
        if (IsKeyDown(KEY_Q)) camera.zoom *= 1.0f - 1.5f * dt;
        camera.zoom = Clamp(camera.zoom, MIN_ZOOM, MAX_ZOOM);
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 d = GetMouseDelta();
            camera.target.x -= d.x / camera.zoom;
            camera.target.y -= d.y / camera.zoom;
        }

        // --- Live cave params: tweak + regenerate -----------------------
        bool regen = false;
        if (IsKeyPressed(KEY_LEFT_BRACKET))  { grid.cave.scale *= 0.8f;  regen = true; }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) { grid.cave.scale *= 1.25f; regen = true; }
        if (IsKeyPressed(KEY_COMMA))         { grid.cave.threshold -= 0.02f; regen = true; }
        if (IsKeyPressed(KEY_PERIOD))        { grid.cave.threshold += 0.02f; regen = true; }
        if (IsKeyPressed(KEY_SEMICOLON))     { grid.cave.octaves--; regen = true; }
        if (IsKeyPressed(KEY_APOSTROPHE))    { grid.cave.octaves++; regen = true; }
        if (IsKeyPressed(KEY_G))             { grid.cave.seed = (unsigned int)GetRandomValue(1, 1000000); regen = true; }
        grid.cave.scale     = Clamp(grid.cave.scale, 0.005f, 0.3f);
        grid.cave.threshold = Clamp(grid.cave.threshold, 0.1f, 0.9f);
        if (grid.cave.octaves < 1) grid.cave.octaves = 1;
        if (grid.cave.octaves > 8) grid.cave.octaves = 8;
        if (regen) GridRegenerate(&grid);

        // --- Stream the window to follow the camera (endless world) -----
        int nox = (int)floorf(camera.target.x / CELL_SIZE) - grid.width / 2;
        int noy = (int)floorf(camera.target.y / CELL_SIZE) - grid.height / 2;
        GridStreamTo(&grid, nox, noy);

        // --- Brush size on the wheel ------------------------------------
        brushRadius += (int)GetMouseWheelMove();
        if (brushRadius < 0)  brushRadius = 0;
        if (brushRadius > 40) brushRadius = 40;

        // --- Paint (screen -> world -> buffer cell) ---------------------
        Vector2 world = GetScreenToWorld2D(GetMousePosition(), camera);
        int bx = (int)floorf(world.x / CELL_SIZE) - grid.originX;
        int by = (int)floorf(world.y / CELL_SIZE) - grid.originY;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            GridPaint(&grid, bx, by, brushRadius, selected);
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            GridPaint(&grid, bx, by, brushRadius, CELL_EMPTY);

        // --- Simulate ----------------------------------------------------
        GridUpdate(&grid);

        // --- Draw world (through camera) into the offscreen target -------
        BeginTextureMode(target);
        ClearBackground((Color){12, 12, 16, 255});
        BeginMode2D(camera);
        GridDraw(&grid, camera);
        EndMode2D();
        EndTextureMode();

        // --- Composite through the glow shader, then HUD on top ----------
        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(bloom);
        DrawTextureRec(target.texture,
                       (Rectangle){0, 0, (float)target.texture.width,
                                   -(float)target.texture.height},
                       (Vector2){0, 0}, WHITE);
        EndShaderMode();

        DrawText(TextFormat("Material: %s   Zoom: %.2f", CellName(selected), camera.zoom),
                 10, 10, 20, RAYWHITE);
        DrawText(TextFormat("Cave  scale: %.4f  threshold: %.2f  octaves: %d  seed: %u",
                            grid.cave.scale, grid.cave.threshold, grid.cave.octaves, grid.cave.seed),
                 10, 34, 18, (Color){150, 220, 150, 255});
        DrawText("1-9/L/R/M/0 materials   WASD pan   Q/E zoom   wheel brush   C clear",
                 10, screenHeight - 48, 18, LIGHTGRAY);
        DrawText("[ ] scale   , . threshold   ; ' octaves   G new seed   (caves stream as you pan)",
                 10, screenHeight - 26, 18, GRAY);
        DrawFPS(screenWidth - 90, 10);

        EndDrawing();
    }

    UnloadShader(bloom);
    UnloadRenderTexture(target);
    GridFree(&grid);
    CloseWindow();
    return 0;
}

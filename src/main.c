//
// main.c - app entry point and game loop. Wires together config, world,
// renderer, audio, HUD and the structure editor; owns input and pause state.
//
#include "core.h"
#include "config.h"
#include "noise.h"
#include "grid.h"
#include "biome.h"
#include "render.h"
#include "audio.h"
#include "hud.h"
#include "structure.h"

#include <raymath.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

// How many screen-sized chunks to simulate across/down (3 => the chunk you're
// in plus the 8 around it).
#define SIM_CHUNKS 3
#define MIN_ZOOM 0.34f   // zoomed out: shows ~the whole simulated 3x3 area
#define MAX_ZOOM 16.0f   // zoomed in: close inspection of individual cells

typedef enum AppState { STATE_MENU, STATE_SETTINGS, STATE_GAME } AppState;

// Start a fresh world: new seed, recentre the camera, regenerate (clears the
// persistence store).
static void NewGame(Grid *g, Camera2D *cam, const AppConfig *cfg) {
    g->cave.biomes = cfg->biomes;
    g->cave.seed = (unsigned)GetRandomValue(1, 1000000);
    cam->target = (Vector2){0, 0};
    cam->zoom = 1.0f;
    g->originX = -g->width / 2;
    g->originY = -g->height / 2;
    GridRegenerate(g);
}

int main(void) {
    AppConfig cfg = Config_Load(AssetPath("config/config.xml"));

    InitWindow(cfg.winW, cfg.winH, cfg.title);
    SetTargetFPS(cfg.fps);
    SetExitKey(KEY_NULL); // ESC toggles the pause menu instead of quitting
    SetRandomSeed((unsigned int)time(NULL));

    // World window: a SIM_CHUNKS x SIM_CHUNKS block of screen-sized chunks,
    // also never smaller than the most zoomed-out view, plus a small margin.
    int chunkW = cfg.winW / CELL_SIZE,  chunkH = cfg.winH / CELL_SIZE;
    int viewW  = (int)(cfg.winW / (float)CELL_SIZE / MIN_ZOOM);
    int viewH  = (int)(cfg.winH / (float)CELL_SIZE / MIN_ZOOM);
    int bufW = (SIM_CHUNKS * chunkW > viewW ? SIM_CHUNKS * chunkW : viewW) + 16;
    int bufH = (SIM_CHUNKS * chunkH > viewH ? SIM_CHUNKS * chunkH : viewH) + 16;
    Grid grid = GridCreate(bufW * CELL_SIZE, bufH * CELL_SIZE);
    grid.cave.scale        = cfg.caveScale;
    grid.cave.threshold    = cfg.caveThreshold;
    grid.cave.mudThreshold = cfg.caveMud;
    grid.cave.octaves      = cfg.caveOctaves;
    grid.cave.biomes       = cfg.biomes;
    grid.cave.seed         = cfg.caveSeed ? cfg.caveSeed : (unsigned)GetRandomValue(1, 1000000);

    // Load hand-made structures BEFORE generating so they appear in the world.
    Structure_LoadAll(AssetPath("structures"));

    Camera2D camera = {
        .target = {0, 0},
        .offset = {cfg.winW / 2.0f, cfg.winH / 2.0f},
        .zoom = 1.0f,
    };
    grid.originX = (int)floorf(camera.target.x / CELL_SIZE) - grid.width / 2;
    grid.originY = (int)floorf(camera.target.y / CELL_SIZE) - grid.height / 2;
    GridRegenerate(&grid);

    Renderer renderer = Render_Init(&cfg);
    Audio_Init(&cfg);
    Hud_Init(&cfg);

    Cell selected = CELL_SAND;
    int  brush = cfg.brushDefault;
    bool paused = false;
    AppState state = STATE_MENU;

    // Structure editor (dev tool): F2 toggles; drag a rectangle to capture.
    bool devMode = false, selecting = false;
    int  sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0; // world-cell selection corners

    const Cell palette[] = {
        CELL_SAND, CELL_WATER, CELL_WOOD, CELL_OIL, CELL_ACID,
        CELL_SNOW, CELL_FIRE, CELL_GAS, CELL_VAPOR,
    };
    const int paletteCount = sizeof(palette) / sizeof(palette[0]);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Audio_Update();

        // --- Intro menu --------------------------------------------------
        if (state == STATE_MENU) {
            BeginDrawing();
            ClearBackground(BLACK);
            MenuAction a = Hud_DrawMenu(cfg.winW, cfg.winH);
            EndDrawing();
            if (a == MENU_NEW_GAME)      { NewGame(&grid, &camera, &cfg); paused = false; state = STATE_GAME; Audio_Play(SFX_UI); }
            else if (a == MENU_SETTINGS) { state = STATE_SETTINGS; Audio_Play(SFX_UI); }
            else if (a == MENU_EXIT)     { break; }
            continue;
        }

        // --- Settings ----------------------------------------------------
        if (state == STATE_SETTINGS) {
            BeginDrawing();
            ClearBackground(BLACK);
            bool back = Hud_DrawSettings(cfg.winW, cfg.winH, &cfg);
            EndDrawing();
            if (back) { // apply render toggles live
                renderer.cfg.bloom = cfg.bloom;
                renderer.cfg.water = cfg.water;
                renderer.cfg.heat  = cfg.heat;
                state = STATE_MENU;
                Audio_Play(SFX_UI);
            }
            continue;
        }

        if (IsKeyPressed(KEY_ESCAPE)) paused = !paused;

        if (paused) {
            BeginDrawing();
            Render_Present(&renderer);
            UIAction a = Hud_DrawPause(cfg.winW, cfg.winH);
            EndDrawing();
            if (a == UI_RESUME)        { paused = false; Audio_Play(SFX_UI); }
            else if (a == UI_NEW_SEED) { grid.cave.seed = (unsigned)GetRandomValue(1, 1000000); GridRegenerate(&grid); Audio_Play(SFX_UI); }
            else if (a == UI_CLEAR)    { GridClear(&grid); Audio_Play(SFX_UI); }
            else if (a == UI_MENU)     { paused = false; state = STATE_MENU; Audio_Play(SFX_UI); }
            continue;
        }

        if (IsKeyPressed(KEY_F2)) { devMode = !devMode; selecting = false; }

        // Material selection
        for (int k = 0; k < paletteCount; k++)
            if (IsKeyPressed(KEY_ONE + k)) selected = palette[k];
        if (IsKeyPressed(KEY_ZERO)) selected = CELL_EMPTY;
        if (IsKeyPressed(KEY_L))    selected = CELL_LAVA;
        if (IsKeyPressed(KEY_R))    selected = CELL_ROCK;
        if (IsKeyPressed(KEY_M))    selected = CELL_MUD;
        if (IsKeyPressed(KEY_V))    selected = CELL_GLASS;
        if (IsKeyPressed(KEY_B))    selected = CELL_METAL;
        if (IsKeyPressed(KEY_N))    selected = CELL_OBSIDIAN;
        if (IsKeyPressed(KEY_C))    GridClear(&grid);

        // Camera
        float pan = 500.0f * dt / camera.zoom;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    camera.target.y -= pan;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  camera.target.y += pan;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  camera.target.x -= pan;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camera.target.x += pan;
        if (IsKeyDown(KEY_E)) camera.zoom *= 1.0f + 1.5f * dt;
        if (IsKeyDown(KEY_Q)) camera.zoom *= 1.0f - 1.5f * dt;
        camera.zoom = Clamp(camera.zoom, MIN_ZOOM, MAX_ZOOM);
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 d = GetMouseDelta();
            camera.target.x -= d.x / camera.zoom;
            camera.target.y -= d.y / camera.zoom;
        }

        // Live cave params
        bool regen = false;
        if (IsKeyPressed(KEY_LEFT_BRACKET))  { grid.cave.scale *= 0.8f;  regen = true; }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) { grid.cave.scale *= 1.25f; regen = true; }
        if (IsKeyPressed(KEY_COMMA))         { grid.cave.threshold -= 0.02f; regen = true; }
        if (IsKeyPressed(KEY_PERIOD))        { grid.cave.threshold += 0.02f; regen = true; }
        if (IsKeyPressed(KEY_SEMICOLON))     { grid.cave.octaves--; regen = true; }
        if (IsKeyPressed(KEY_APOSTROPHE))    { grid.cave.octaves++; regen = true; }
        if (IsKeyPressed(KEY_G))             { grid.cave.seed = (unsigned)GetRandomValue(1, 1000000); regen = true; }
        grid.cave.scale     = Clamp(grid.cave.scale, 0.005f, 0.3f);
        grid.cave.threshold = Clamp(grid.cave.threshold, 0.1f, 0.9f);
        grid.cave.octaves   = (int)Clamp((float)grid.cave.octaves, 1, 8);
        if (regen) GridRegenerate(&grid);

        // Stream world to follow camera
        int nox = (int)floorf(camera.target.x / CELL_SIZE) - grid.width / 2;
        int noy = (int)floorf(camera.target.y / CELL_SIZE) - grid.height / 2;
        GridStreamTo(&grid, nox, noy);

        // Mouse -> world cell
        Vector2 world = GetScreenToWorld2D(GetMousePosition(), camera);
        int wcx = (int)floorf(world.x / CELL_SIZE), wcy = (int)floorf(world.y / CELL_SIZE);

        if (devMode) {
            // Rectangle capture: drag with the left button, save on release.
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { selecting = true; sx0 = sx1 = wcx; sy0 = sy1 = wcy; }
            if (selecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { sx1 = wcx; sy1 = wcy; }
            if (selecting && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                selecting = false;
                int minX = sx0 < sx1 ? sx0 : sx1, maxX = sx0 > sx1 ? sx0 : sx1;
                int minY = sy0 < sy1 ? sy0 : sy1, maxY = sy0 > sy1 ? sy0 : sy1;
                int w = maxX - minX + 1, h = maxY - minY + 1;
                if (w >= 1 && h >= 1 && w <= 110 && h <= 110) {
                    Cell *buf = MemAlloc((unsigned)(w * h) * sizeof(Cell));
                    for (int ly = 0; ly < h; ly++)
                        for (int lx = 0; lx < w; lx++) {
                            int bx = (minX + lx) - grid.originX, by = (minY + ly) - grid.originY;
                            buf[ly * w + lx] = GridInBounds(&grid, bx, by) ? GridGet(&grid, bx, by) : CELL_EMPTY;
                        }
                    char path[512];
                    snprintf(path, sizeof path, "%sstructures/struct_%u.txt",
                             GetApplicationDirectory(), (unsigned)GetRandomValue(1000, 999999));
                    if (Structure_AddAndSave(w, h, buf, path)) { GridRegenerate(&grid); Audio_Play(SFX_UI); }
                    MemFree(buf);
                }
            }
        } else {
            // Paint
            brush += (int)GetMouseWheelMove();
            brush = (int)Clamp((float)brush, 0, (float)cfg.brushMax);
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                GridPaint(&grid, wcx - grid.originX, wcy - grid.originY, brush, selected);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) Audio_Play(SFX_PLACE);
            }
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
                GridPaint(&grid, wcx - grid.originX, wcy - grid.originY, brush, CELL_EMPTY);
        }

        GridUpdate(&grid);

        // Render
        Biome biome = grid.cave.biomes
            ? BiomeAt(grid.originX + grid.width / 2, grid.originY + grid.height / 2)
            : BIOME_ROCKY;
        Render_Frame(&renderer, &grid, camera, biome);

        BeginDrawing();
        ClearBackground(BLACK);
        Render_Present(&renderer);

        // Selection overlay (drawn in screen space over the composited frame).
        if (devMode && selecting) {
            int minX = sx0 < sx1 ? sx0 : sx1, maxX = sx0 > sx1 ? sx0 : sx1;
            int minY = sy0 < sy1 ? sy0 : sy1, maxY = sy0 > sy1 ? sy0 : sy1;
            Vector2 a = GetWorldToScreen2D((Vector2){minX * CELL_SIZE, minY * CELL_SIZE}, camera);
            Vector2 b = GetWorldToScreen2D((Vector2){(maxX + 1) * CELL_SIZE, (maxY + 1) * CELL_SIZE}, camera);
            DrawRectangleLinesEx((Rectangle){a.x, a.y, b.x - a.x, b.y - a.y}, 2.0f, YELLOW);
        }

        Hud_DrawGame(&grid, selected, BIOMES[biome].name, brush, camera.zoom);
        if (devMode)
            DrawText("DEV: drag to capture a structure (saved + placed on regen).  F2 to exit",
                     10, 100, 18, YELLOW);
        DrawFPS(cfg.winW - 90, 10);
        EndDrawing();
    }

    Hud_Free();
    Audio_Close();
    Render_Free(&renderer);
    Structure_FreeAll();
    GridFree(&grid);
    CloseWindow();
    return 0;
}

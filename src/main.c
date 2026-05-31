//
// main.c - app entry point and game loop. Wires together config, world,
// renderer, audio, HUD, the structure editor/manager and the simulation
// worker thread; owns input and high-level state.
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
#include "strmgr.h"
#include "editor.h"
#include "npc.h"
#include "sim.h"

#include <raymath.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

// How many screen-sized chunks to simulate across/down. 5 => a wide 5x5 block
// around you, so the simulation keeps running well outside the visible area.
#define SIM_CHUNKS 5
// MIN_ZOOM is matched so the most zoomed-out view stays within the simulated
// buffer (no visible static/edge): 5 chunks * 160 cells * 8px = 6400px wide,
// shown in a 1280px window => ~0.20; a touch under that to leave a margin.
#define MIN_ZOOM 0.18f   // zoom way out and survey the whole live region
#define MAX_ZOOM 16.0f   // zoomed in: close inspection of individual cells

typedef enum AppState { STATE_MENU, STATE_SETTINGS, STATE_STRUCTURES, STATE_EDITOR, STATE_GAME } AppState;

// Start a fresh world: new seed, recentre the camera, regenerate (clears the
// persistence store). Caller must hold the sim lock.
static void NewGame(Grid *g, Camera2D *cam, const AppConfig *cfg) {
    g->cave.biomes = cfg->biomes;
    g->cave.seed = (unsigned)GetRandomValue(1, 1000000);
    cam->target = (Vector2){0, 0};
    cam->zoom = 1.0f;
    g->originX = -g->width / 2;
    g->originY = -g->height / 2;
    GridRegenerate(g);
    Npc_Reset();
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
    Npc_Reset();
    GridSnapshot(&grid); // a valid first frame for the renderer

    Renderer renderer = Render_Init(&cfg);
    Audio_Init(&cfg);
    Hud_Init(&cfg);
    StrMgr_Init();

    // Spin up the background simulation worker. If it fails to start we fall
    // back to stepping the grid on the main thread (Sim_IsThreaded() == false).
    Sim_Start(&grid, cfg.fps);

    Cell selected = CELL_SAND;
    int  brush = cfg.brushDefault;
    bool paused = false;
    AppState state = STATE_MENU;

    // Structure editor (dev tool): F2 toggles; drag a rectangle to capture.
    bool devMode = false, selecting = false;
    int  sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0; // world-cell selection corners

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Audio_Update();

        // The worker only steps during active gameplay.
        Sim_SetActive(state == STATE_GAME && !paused && !devMode);

        // --- Intro menu --------------------------------------------------
        if (state == STATE_MENU) {
            BeginDrawing();
            ClearBackground(BLACK);
            MenuAction a = Hud_DrawMenu(cfg.winW, cfg.winH);
            EndDrawing();
            if (a == MENU_NEW_GAME)        { Sim_Lock(); NewGame(&grid, &camera, &cfg); GridSnapshot(&grid); Sim_Unlock(); paused = false; devMode = false; state = STATE_GAME; Audio_Play(SFX_UI); }
            else if (a == MENU_STRUCTURES) { StrMgr_Refresh(); state = STATE_STRUCTURES; Audio_Play(SFX_UI); }
            else if (a == MENU_SETTINGS)   { state = STATE_SETTINGS; Audio_Play(SFX_UI); }
            else if (a == MENU_EXIT)       { break; }
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

        // --- Structure Manager -------------------------------------------
        if (state == STATE_STRUCTURES) {
            BeginDrawing();
            ClearBackground(BLACK);
            StrMgrAction a = StrMgr_Draw(cfg.winW, cfg.winH);
            EndDrawing();
            if (a == STRMGR_BACK) { state = STATE_MENU; Audio_Play(SFX_UI); }
            else if (a == STRMGR_NEW) {
                // Open the dedicated blank-canvas editor.
                Editor_Open();
                state = STATE_EDITOR; Audio_Play(SFX_UI);
            }
            continue;
        }

        // --- Structure Editor (blank canvas) -----------------------------
        if (state == STATE_EDITOR) {
            BeginDrawing();
            ClearBackground(BLACK);
            EditorAction a = Editor_Run(cfg.winW, cfg.winH);
            EndDrawing();
            if (a == EDITOR_BACK) {
                Editor_Close();
                StrMgr_Refresh();
                state = STATE_STRUCTURES;
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
            else if (a == UI_NEW_SEED) { Sim_Lock(); grid.cave.seed = (unsigned)GetRandomValue(1, 1000000); GridRegenerate(&grid); Npc_Reset(); GridSnapshot(&grid); Sim_Unlock(); Audio_Play(SFX_UI); }
            else if (a == UI_CLEAR)    { Sim_Lock(); GridClear(&grid); GridSnapshot(&grid); Sim_Unlock(); Audio_Play(SFX_UI); }
            else if (a == UI_MENU)     { paused = false; devMode = false; state = STATE_MENU; Audio_Play(SFX_UI); }
            continue;
        }

        if (IsKeyPressed(KEY_F2)) { devMode = !devMode; selecting = false; }

        // Material selection is now via the on-screen palette (Hud_DrawPalette).
        // Suppress world painting while the cursor is over that panel.
        bool overUI = CheckCollisionPointRec(GetMousePosition(), Hud_PaletteRect(cfg.winW, cfg.winH));

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

        // Live cave params (decide here, regenerate under the lock below).
        bool regen = false;
        if (IsKeyPressed(KEY_LEFT_BRACKET))  { grid.cave.scale *= 0.8f;  regen = true; }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) { grid.cave.scale *= 1.25f; regen = true; }
        if (IsKeyPressed(KEY_COMMA))         { grid.cave.threshold -= 0.02f; regen = true; }
        if (IsKeyPressed(KEY_PERIOD))        { grid.cave.threshold += 0.02f; regen = true; }
        if (IsKeyPressed(KEY_SEMICOLON))     { grid.cave.octaves--; regen = true; }
        if (IsKeyPressed(KEY_APOSTROPHE))    { grid.cave.octaves++; regen = true; }
        if (IsKeyPressed(KEY_G))             { grid.cave.seed = (unsigned)GetRandomValue(1, 1000000); regen = true; }
        bool clear = IsKeyPressed(KEY_C);
        grid.cave.scale     = Clamp(grid.cave.scale, 0.005f, 0.3f);
        grid.cave.threshold = Clamp(grid.cave.threshold, 0.1f, 0.9f);
        grid.cave.octaves   = (int)Clamp((float)grid.cave.octaves, 1, 8);

        int wcx = 0, wcy = 0;
        int wheel = (int)GetMouseWheelMove();

        // All grid mutation + the render snapshot happen under the sim lock so
        // the worker thread never sees a half-edited world. The expensive draw
        // (Render_Frame) runs afterwards, unlocked, in parallel with the worker.
        Sim_Lock();
        {
            if (regen) GridRegenerate(&grid);
            if (clear) GridClear(&grid);

            int nox = (int)floorf(camera.target.x / CELL_SIZE) - grid.width / 2;
            int noy = (int)floorf(camera.target.y / CELL_SIZE) - grid.height / 2;
            GridStreamTo(&grid, nox, noy);

            Vector2 world = GetScreenToWorld2D(GetMousePosition(), camera);
            wcx = (int)floorf(world.x / CELL_SIZE);
            wcy = (int)floorf(world.y / CELL_SIZE);

            if (devMode) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUI) { selecting = true; sx0 = sx1 = wcx; sy0 = sy1 = wcy; }
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
                        if (Structure_AddAndSave(w, h, buf, path)) { GridRegenerate(&grid); StrMgr_Refresh(); Audio_Play(SFX_UI); }
                        MemFree(buf);
                    }
                }
            } else {
                if (!overUI) brush += wheel;
                brush = (int)Clamp((float)brush, 0, (float)cfg.brushMax);
                if (!overUI && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    GridPaint(&grid, wcx - grid.originX, wcy - grid.originY, brush, selected);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) Audio_Play(SFX_PLACE);
                }
                if (!overUI && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
                    GridPaint(&grid, wcx - grid.originX, wcy - grid.originY, brush, CELL_EMPTY);
            }

            // Critters (frogs/flies) read & write the grid - keep them in the
            // locked region. They don't run while capturing structures.
            if (!devMode) Npc_Update(&grid, dt);

            // Single-threaded fallback: advance the sim ourselves.
            if (!Sim_IsThreaded() && !devMode) GridUpdate(&grid);

            GridSnapshot(&grid); // consistent copy for the renderer
        }
        Sim_Unlock();

        // Render (reads the snapshot; safe to run while the worker steps).
        Biome biome = grid.cave.biomes
            ? BiomeAt(grid.rOriginX + grid.width / 2, grid.rOriginY + grid.height / 2)
            : BIOME_ROCKY;
        Render_Frame(&renderer, &grid, camera, biome);

        BeginDrawing();
        ClearBackground(BLACK);
        Render_Present(&renderer);

        // Critters drawn in world space on top of the composited frame.
        BeginMode2D(camera);
        Npc_Draw();
        EndMode2D();

        // Selection overlay (drawn in screen space over the composited frame).
        if (devMode && selecting) {
            int minX = sx0 < sx1 ? sx0 : sx1, maxX = sx0 > sx1 ? sx0 : sx1;
            int minY = sy0 < sy1 ? sy0 : sy1, maxY = sy0 > sy1 ? sy0 : sy1;
            Vector2 a = GetWorldToScreen2D((Vector2){minX * CELL_SIZE, minY * CELL_SIZE}, camera);
            Vector2 b = GetWorldToScreen2D((Vector2){(maxX + 1) * CELL_SIZE, (maxY + 1) * CELL_SIZE}, camera);
            DrawRectangleLinesEx((Rectangle){a.x, a.y, b.x - a.x, b.y - a.y}, 2.0f, YELLOW);
        }

        int posX = (int)floorf(camera.target.x / CELL_SIZE);
        int posY = (int)floorf(camera.target.y / CELL_SIZE);
        Hud_DrawGame(&grid, selected, BIOMES[biome].name, brush, camera.zoom, posX, posY);
        Hud_DrawPalette(&selected, &brush, cfg.brushMax, cfg.winW, cfg.winH);
        if (devMode)
            DrawText("DEV: drag to capture a structure (saved + placed on regen).  F2 to exit",
                     10, 124, 18, YELLOW);
        DrawFPS(cfg.winW - 90, 10);
        EndDrawing();
    }

    Sim_Stop();
    Editor_Close();
    StrMgr_Free();
    Hud_Free();
    Audio_Close();
    Render_Free(&renderer);
    Structure_FreeAll();
    GridFree(&grid);
    CloseWindow();
    return 0;
}

//
// editor.c - Structure Editor scene (see editor.h).
//
// A blank canvas with the full simulation available, plus a proper tool box:
//   BRUSH  - freehand painting (right mouse erases)
//   LINE   - drag a straight line, stamped with the brush thickness
//   RECT   - drag a rectangle (outline, or filled via the Filled toggle)
//   CIRCLE - drag from centre outward (outline or filled)
//   FILL   - flood-fill the clicked region (right mouse flood-erases)
//   PICK   - eyedropper: click a cell to select its material
// Materials are chosen from the swatch grid in the side panel (same list as
// the in-game palette).
//
#include "editor.h"
#include "core.h"
#include "grid.h"
#include "materials.h"
#include "structure.h"
#include "hud.h"

#include "raygui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CANVAS   96     // canvas is CANVAS x CANVAS cells (<= structure limit)
#define PANEL_W  300.0f // width of the right-hand tool / stats panel

typedef enum Tool { TOOL_BRUSH = 0, TOOL_LINE, TOOL_RECT, TOOL_CIRCLE, TOOL_FILL, TOOL_PICK } Tool;

static struct {
    Grid     grid;
    bool     open;
    bool     simulate;   // is the sim running (Space / panel button)
    Cell     selected;
    int      brush;
    int      tool;       // Tool enum (int for GuiToggleGroup)
    bool     filled;     // rect/circle: filled instead of outline
    bool     dragging;   // shape drag in progress
    int      dragX, dragY;
    char     status[96];
    float    statusT;    // seconds remaining on the status toast
    int      counts[CELL_COUNT];
    int      used;       // total non-empty cells
} E;

void Editor_Open(void) {
    if (!E.open) {
        E.grid = GridCreate(CANVAS * CELL_SIZE, CANVAS * CELL_SIZE);
        E.open = true;
    }
    GridClear(&E.grid);
    E.grid.originX = 0; E.grid.originY = 0;
    E.simulate = true;
    E.selected = CELL_SAND;
    E.brush = 2;
    E.tool = TOOL_BRUSH;
    E.filled = false;
    E.dragging = false;
    E.status[0] = '\0';
    E.statusT = 0.0f;
}

void Editor_Close(void) {
    if (E.open) { GridFree(&E.grid); E.open = false; }
}

static void SetStatus(const char *s) {
    snprintf(E.status, sizeof E.status, "%s", s);
    E.statusT = 2.5f;
}

// Tally how many of each material is currently on the canvas.
static void CountCells(void) {
    for (int i = 0; i < CELL_COUNT; i++) E.counts[i] = 0;
    E.used = 0;
    int n = E.grid.width * E.grid.height;
    for (int i = 0; i < n; i++) {
        Cell c = E.grid.cells[i];
        E.counts[c]++;
        if (c != CELL_EMPTY) E.used++;
    }
}

// Trim to the non-empty bounding box and save as a structure.
static void SaveCanvas(void) {
    int minX = CANVAS, minY = CANVAS, maxX = -1, maxY = -1;
    for (int y = 0; y < E.grid.height; y++)
        for (int x = 0; x < E.grid.width; x++)
            if (E.grid.cells[y * E.grid.width + x] != CELL_EMPTY) {
                if (x < minX) minX = x; if (x > maxX) maxX = x;
                if (y < minY) minY = y; if (y > maxY) maxY = y;
            }
    if (maxX < 0) { SetStatus("Canvas is empty"); return; }

    int w = maxX - minX + 1, h = maxY - minY + 1;
    Cell *buf = MemAlloc((unsigned)(w * h) * sizeof(Cell));
    for (int ly = 0; ly < h; ly++)
        for (int lx = 0; lx < w; lx++)
            buf[ly * w + lx] = E.grid.cells[(minY + ly) * E.grid.width + (minX + lx)];

    char path[512];
    snprintf(path, sizeof path, "%sstructures/struct_%u.txt",
             GetApplicationDirectory(), (unsigned)GetRandomValue(1000, 999999));
    bool ok = Structure_AddAndSave(w, h, buf, path);
    MemFree(buf);
    SetStatus(ok ? TextFormat("Saved %dx%d structure", w, h) : "Save failed");
}

// --- stamping helpers --------------------------------------------------------
static void StampCell(int x, int y, Cell mat) {
    GridPaint(&E.grid, x, y, E.brush, mat);
}

// Bresenham line, stamped with the brush radius at every step.
static void StampLine(int x0, int y0, int x1, int y1, Cell mat) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        StampCell(x0, y0, mat);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void StampRect(int x0, int y0, int x1, int y1, Cell mat) {
    int minX = x0 < x1 ? x0 : x1, maxX = x0 > x1 ? x0 : x1;
    int minY = y0 < y1 ? y0 : y1, maxY = y0 > y1 ? y0 : y1;
    if (E.filled) {
        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++)
                if (GridInBounds(&E.grid, x, y)) GridSet(&E.grid, x, y, mat);
    } else {
        StampLine(minX, minY, maxX, minY, mat);
        StampLine(maxX, minY, maxX, maxY, mat);
        StampLine(maxX, maxY, minX, maxY, mat);
        StampLine(minX, maxY, minX, minY, mat);
    }
}

static void StampCircle(int cx, int cy, int r, Cell mat) {
    if (r < 1) { StampCell(cx, cy, mat); return; }
    if (E.filled) {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++) {
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy <= r * r && GridInBounds(&E.grid, x, y))
                    GridSet(&E.grid, x, y, mat);
            }
    } else {
        int steps = 16 + r * 8; // walk the circumference, stamping with the brush
        for (int k = 0; k < steps; k++) {
            float a = (float)k / steps * 2.0f * PI;
            StampCell(cx + (int)roundf(cosf(a) * r), cy + (int)roundf(sinf(a) * r), mat);
        }
    }
}

// Flood-fill the 4-connected region of the clicked cell's material.
static void FloodFill(int x, int y, Cell repl) {
    if (!GridInBounds(&E.grid, x, y)) return;
    Cell target = GridGet(&E.grid, x, y);
    if (target == repl) return;
    int w = E.grid.width, h = E.grid.height;
    int cap = w * h * 4;
    int *stack = MemAlloc((unsigned)cap * sizeof(int));
    int top = 0;
    stack[top++] = y * w + x;
    while (top > 0) {
        int i = stack[--top];
        int ix = i % w, iy = i / w;
        if (GridGet(&E.grid, ix, iy) != target) continue;
        GridSet(&E.grid, ix, iy, repl);
        if (ix > 0     && top < cap && GridGet(&E.grid, ix - 1, iy) == target) stack[top++] = i - 1;
        if (ix < w - 1 && top < cap && GridGet(&E.grid, ix + 1, iy) == target) stack[top++] = i + 1;
        if (iy > 0     && top < cap && GridGet(&E.grid, ix, iy - 1) == target) stack[top++] = i - w;
        if (iy < h - 1 && top < cap && GridGet(&E.grid, ix, iy + 1) == target) stack[top++] = i + w;
    }
    MemFree(stack);
}

// --- side panel ----------------------------------------------------------------
static void DrawPanel(int screenW, int screenH, EditorAction *action) {
    float px = screenW - PANEL_W;
    DrawRectangle((int)px, 0, (int)PANEL_W, screenH, (Color){16, 18, 24, 255});
    DrawRectangleLines((int)px, 0, (int)PANEL_W, screenH, (Color){60, 70, 90, 255});

    float x = px + 14.0f, w = PANEL_W - 28.0f, y = 12.0f;
    DrawText("STRUCTURE EDITOR", (int)x, (int)y, 20, (Color){235, 240, 255, 255});
    y += 30;

    // Tools (two rows of three).
    GuiToggleGroup((Rectangle){x, y, (w - 8) / 3, 24}, "BRUSH;LINE;RECT\nCIRCLE;FILL;PICK", &E.tool);
    y += 58;
    GuiCheckBox((Rectangle){x, y, 18, 18}, "Filled shapes", &E.filled);
    y += 28;

    // Material swatches (8 columns).
    DrawText("Materials:", (int)x, (int)y, 15, (Color){150, 160, 175, 255});
    y += 20;
    int palCount = 0;
    const Cell *pal = Hud_PaletteList(&palCount);
    const int cols = 8;
    float gap = 4.0f, sw = (w - (cols - 1) * gap) / cols;
    Vector2 m = GetMousePosition();
    for (int i = 0; i < palCount; i++) {
        int gx = i % cols, gy = i / cols;
        Rectangle s = {x + gx * (sw + gap), y + gy * (sw + gap), sw, sw};
        Cell mat = pal[i];
        bool hover = CheckCollisionPointRec(m, s);
        DrawRectangleRounded(s, 0.25f, 4, (Color){10, 12, 16, 255});
        if (mat == CELL_EMPTY) {
            DrawText("E", (int)(s.x + sw / 2 - 4), (int)(s.y + sw / 2 - 7), 14, (Color){225, 130, 130, 255});
        } else {
            Color col = MATERIALS[mat].color; col.a = 255;
            DrawRectangleRounded((Rectangle){s.x + 2, s.y + 2, sw - 4, sw - 4}, 0.25f, 4, col);
        }
        Color border = (E.selected == mat) ? (Color){255, 230, 120, 255}
                       : (hover ? (Color){185, 205, 235, 255} : (Color){70, 80, 100, 140});
        DrawRectangleRoundedLinesEx(s, 0.25f, 4, (E.selected == mat) ? 2.0f : 1.0f, border);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) E.selected = mat;
        if (hover) { // tooltip with the material name, left of the panel
            const char *nm = CellName(mat);
            int tw = MeasureText(nm, 15);
            DrawRectangle((int)(px - tw - 16), (int)(m.y - 12), tw + 10, 20, (Color){16, 18, 24, 235});
            DrawText(nm, (int)(px - tw - 11), (int)(m.y - 9), 15, RAYWHITE);
        }
    }
    int rows = (palCount + cols - 1) / cols;
    y += rows * (sw + gap) + 8;

    // Brush size.
    DrawText(TextFormat("Brush: %d   (%s)", E.brush, CellName(E.selected)),
             (int)x, (int)y, 15, (Color){200, 205, 215, 255});
    y += 20;
    float bf = (float)E.brush;
    GuiSliderBar((Rectangle){x, y, w, 14}, NULL, NULL, &bf, 0, 20);
    E.brush = (int)(bf + 0.5f);
    y += 26;

    // Stats: total + top-6 composition.
    DrawText(TextFormat("Pixels used: %d", E.used), (int)x, (int)y, 16, (Color){150, 200, 160, 255});
    y += 22;
    bool shown[CELL_COUNT] = {0};
    for (int line = 0; line < 6; line++) {
        int best = -1, bestN = 0;
        for (int c = 1; c < CELL_COUNT; c++)
            if (!shown[c] && E.counts[c] > bestN) { bestN = E.counts[c]; best = c; }
        if (best < 0) break;
        shown[best] = true;
        DrawRectangle((int)x, (int)y, 12, 12, MATERIALS[best].color);
        DrawText(TextFormat("%-12s %5d", CellName((Cell)best), bestN),
                 (int)(x + 18), (int)y, 14, (Color){210, 215, 225, 255});
        y += 17;
    }

    // Bottom buttons.
    float bh = 38, bgap = 9;
    float by = screenH - (bh + bgap) * 4 - 12;
    if (GuiButton((Rectangle){x, by + 0 * (bh + bgap), w, bh},
                  E.simulate ? "Pause Simulation" : "Play Simulation")) E.simulate = !E.simulate;
    if (GuiButton((Rectangle){x, by + 1 * (bh + bgap), w, bh}, "Clear Canvas")) { GridClear(&E.grid); SetStatus("Cleared"); }
    if (GuiButton((Rectangle){x, by + 2 * (bh + bgap), w, bh}, "Save Structure")) SaveCanvas();
    if (GuiButton((Rectangle){x, by + 3 * (bh + bgap), w, bh}, "Back")) *action = EDITOR_BACK;

    DrawText("LMB draw   RMB erase/cancel   wheel brush   Space play/pause   Esc back",
             18, screenH - 24, 14, (Color){120, 130, 145, 255});
    if (E.statusT > 0.0f)
        DrawText(E.status, 18, screenH - 48, 18, Fade((Color){255, 230, 140, 255}, fminf(E.statusT, 1.0f)));
}

// --- main loop -------------------------------------------------------------------
EditorAction Editor_Run(int screenW, int screenH) {
    EditorAction action = EDITOR_STAY;
    float dt = GetFrameTime();
    float canvasViewW = screenW - PANEL_W;

    // Camera fits the whole canvas into the left viewport.
    float canvasPx = CANVAS * CELL_SIZE;
    float zoom = fminf(canvasViewW / canvasPx, screenH / canvasPx) * 0.92f;
    Camera2D cam = {
        .target = {canvasPx * 0.5f, canvasPx * 0.5f},
        .offset = {canvasViewW * 0.5f, screenH * 0.5f},
        .zoom   = zoom,
    };

    // --- input ----------------------------------------------------------------
    if (IsKeyPressed(KEY_ESCAPE)) action = EDITOR_BACK;
    if (IsKeyPressed(KEY_SPACE)) E.simulate = !E.simulate;

    Vector2 mpos = GetMousePosition();
    bool overCanvas = mpos.x < canvasViewW;
    Vector2 world = GetScreenToWorld2D(mpos, cam);
    int cx = (int)floorf(world.x / CELL_SIZE), cy = (int)floorf(world.y / CELL_SIZE);

    if (overCanvas) {
        E.brush += (int)GetMouseWheelMove();
        if (E.brush < 0) E.brush = 0; if (E.brush > 20) E.brush = 20;

        switch ((Tool)E.tool) {
            case TOOL_BRUSH:
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))  GridPaint(&E.grid, cx, cy, E.brush, E.selected);
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) GridPaint(&E.grid, cx, cy, E.brush, CELL_EMPTY);
                break;
            case TOOL_LINE:
            case TOOL_RECT:
            case TOOL_CIRCLE:
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { E.dragging = true; E.dragX = cx; E.dragY = cy; }
                if (E.dragging && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) E.dragging = false; // cancel
                if (E.dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    E.dragging = false;
                    if (E.tool == TOOL_LINE) StampLine(E.dragX, E.dragY, cx, cy, E.selected);
                    else if (E.tool == TOOL_RECT) StampRect(E.dragX, E.dragY, cx, cy, E.selected);
                    else {
                        int dx = cx - E.dragX, dy = cy - E.dragY;
                        StampCircle(E.dragX, E.dragY, (int)roundf(sqrtf((float)(dx * dx + dy * dy))), E.selected);
                    }
                }
                break;
            case TOOL_FILL:
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  FloodFill(cx, cy, E.selected);
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) FloodFill(cx, cy, CELL_EMPTY);
                break;
            case TOOL_PICK:
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && GridInBounds(&E.grid, cx, cy)) {
                    E.selected = GridGet(&E.grid, cx, cy);
                    SetStatus(TextFormat("Picked: %s", CellName(E.selected)));
                }
                break;
        }
    }

    // --- simulate ----------------------------------------------------------------
    if (E.simulate) GridUpdate(&E.grid);
    GridSnapshot(&E.grid);
    CountCells();
    if (E.statusT > 0.0f) E.statusT -= dt;

    // --- draw the canvas -----------------------------------------------------------
    BeginScissorMode(0, 0, (int)canvasViewW, screenH);
    BeginMode2D(cam);
    DrawRectangle(0, 0, (int)canvasPx, (int)canvasPx, (Color){6, 7, 9, 255}); // the canvas
    GridDrawWorld(&E.grid, cam);
    DrawRectangleLinesEx((Rectangle){0, 0, canvasPx, canvasPx}, 1.5f / zoom, (Color){70, 80, 100, 255});

    // Tool previews (world space, translucent in the selected material colour).
    Color pc = MATERIALS[E.selected].color; pc.a = 130;
    if (E.selected == CELL_EMPTY) pc = (Color){220, 120, 120, 110};
    float thick = (2.0f * E.brush + 1.0f) * CELL_SIZE;
    Vector2 a = {(E.dragX + 0.5f) * CELL_SIZE, (E.dragY + 0.5f) * CELL_SIZE};
    Vector2 b = {(cx + 0.5f) * CELL_SIZE, (cy + 0.5f) * CELL_SIZE};
    if (E.dragging && E.tool == TOOL_LINE) {
        DrawLineEx(a, b, thick, pc);
    } else if (E.dragging && E.tool == TOOL_RECT) {
        float rx = fminf(a.x, b.x) - CELL_SIZE * 0.5f, ry = fminf(a.y, b.y) - CELL_SIZE * 0.5f;
        float rw = fabsf(a.x - b.x) + CELL_SIZE, rh = fabsf(a.y - b.y) + CELL_SIZE;
        if (E.filled) DrawRectangleRec((Rectangle){rx, ry, rw, rh}, pc);
        else          DrawRectangleLinesEx((Rectangle){rx, ry, rw, rh}, thick, pc);
    } else if (E.dragging && E.tool == TOOL_CIRCLE) {
        float r = sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        if (E.filled) DrawCircleV(a, r, pc);
        else          DrawRing(a, fmaxf(r - thick * 0.5f, 0), r + thick * 0.5f, 0, 360, 48, pc);
    } else if (overCanvas && (E.tool == TOOL_BRUSH)) {
        DrawCircleLines((int)b.x, (int)b.y, (E.brush + 0.5f) * CELL_SIZE, Fade(RAYWHITE, 0.5f));
    } else if (overCanvas && E.tool == TOOL_FILL) {
        DrawRectangleLinesEx((Rectangle){cx * (float)CELL_SIZE, cy * (float)CELL_SIZE,
                                         CELL_SIZE, CELL_SIZE}, 2.0f, Fade(pc, 0.9f));
    }
    EndMode2D();
    EndScissorMode();

    // --- side panel ------------------------------------------------------------------
    DrawPanel(screenW, screenH, &action);
    return action;
}

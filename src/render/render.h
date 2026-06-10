//
// render.h - parallax background + offscreen scene + post-processing shaders.
//
#ifndef RENDER_H
#define RENDER_H

#include "core.h"
#include "config.h"
#include "grid.h"
#include "biome.h"

#define PARALLAX_LAYERS 3

typedef struct Renderer {
    int w, h;
    AppConfig cfg;
    RenderTexture2D scene;       // world is drawn here
    RenderTexture2D ping, pong;  // post-processing ping-pong targets
    Shader water, heat, bloom;   // loaded from shaders/*.fs
    Texture2D parallax[PARALLAX_LAYERS];
    Texture2D finalTex;          // result of the last post pass (not owned)
    // World fast path: the snapshot is converted to one texel per cell and
    // drawn as a single scaled quad (instead of one rectangle per cell).
    Texture2D worldTex;
    Color    *pixels;
    int       gw, gh;            // worldTex dimensions, in grid cells
} Renderer;

Renderer Render_Init(const AppConfig *cfg);
void Render_Free(Renderer *r);

// Render the world + background into the offscreen target and run the post
// chain. `biome` selects background colours. Call before BeginDrawing().
void Render_Frame(Renderer *r, const Grid *grid, Camera2D camera, Biome biome);

// Blit the finished frame to the screen. Call inside BeginDrawing().
void Render_Present(const Renderer *r);

#endif // RENDER_H

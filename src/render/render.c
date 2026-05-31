//
// render.c - background parallax and the post-processing shader chain.
//
#include "render.h"
#include "noise.h"

#include <math.h>

// Build one parallax silhouette layer: a jagged noise skyline filling the
// bottom of a transparent texture. Tinted later per biome via DrawTexture.
static Texture2D MakeLayer(int w, int h, int seed, float amp, float baseFrac) {
    Image img = GenImageColor(w, h, BLANK);
    int base = (int)(h * baseFrac);
    for (int x = 0; x < w; x++) {
        float n = NoiseFbm(x * 0.012f + seed * 13.0f, seed * 7.0f, 3); // 0..1
        int top = base - (int)(n * amp);
        if (top < 0) top = 0;
        ImageDrawLine(&img, x, top, x, h, WHITE); // white so tint controls colour
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

Renderer Render_Init(const AppConfig *cfg) {
    Renderer r = {0};
    r.cfg = *cfg;
    r.w = cfg->winW;
    r.h = cfg->winH;

    r.scene = LoadRenderTexture(r.w, r.h);
    r.ping  = LoadRenderTexture(r.w, r.h);
    r.pong  = LoadRenderTexture(r.w, r.h);

    r.water = LoadShader(0, AssetPath("shaders/water.glsl"));
    r.heat  = LoadShader(0, AssetPath("shaders/heat.glsl"));
    r.bloom = LoadShader(0, AssetPath("shaders/bloom.glsl"));

    // Resolution uniform is constant; set once per shader.
    float res[2] = {(float)r.w, (float)r.h};
    Shader sh[3] = {r.water, r.heat, r.bloom};
    for (int i = 0; i < 3; i++)
        SetShaderValue(sh[i], GetShaderLocation(sh[i], "resolution"), res, SHADER_UNIFORM_VEC2);

    // Far layers move least; near layers most. Seeds differ for variety.
    r.parallax[0] = MakeLayer(r.w, r.h, 1, r.h * 0.25f, 0.55f);
    r.parallax[1] = MakeLayer(r.w, r.h, 2, r.h * 0.40f, 0.72f);
    r.parallax[2] = MakeLayer(r.w, r.h, 3, r.h * 0.55f, 0.90f);
    return r;
}

void Render_Free(Renderer *r) {
    UnloadRenderTexture(r->scene);
    UnloadRenderTexture(r->ping);
    UnloadRenderTexture(r->pong);
    UnloadShader(r->water);
    UnloadShader(r->heat);
    UnloadShader(r->bloom);
    for (int i = 0; i < PARALLAX_LAYERS; i++) UnloadTexture(r->parallax[i]);
    *r = (Renderer){0};
}

static Color Lerp8(Color a, Color b, float t) {
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Draw sky gradient + parallax silhouettes. Camera drives the horizontal/
// vertical offset of each layer by a different factor (the parallax effect).
static void DrawParallax(const Renderer *r, Camera2D cam, Biome biome) {
    const BiomeInfo *bi = &BIOMES[biome];
    DrawRectangleGradientV(0, 0, r->w, r->h, bi->skyTop, bi->skyBottom);

    const float factor[PARALLAX_LAYERS] = {0.08f, 0.18f, 0.35f};
    for (int i = 0; i < PARALLAX_LAYERS; i++) {
        float ox = -cam.target.x * factor[i];
        float oy =  cam.target.y * factor[i] * 0.15f;
        // Wrap horizontally and draw two copies for a seamless scroll.
        float wrapped = fmodf(ox, (float)r->w);
        if (wrapped > 0) wrapped -= r->w;
        // Subtle: blend the hill colour most of the way toward the sky and make
        // it semi-transparent, so the background reads as faint distant layers.
        Color tint = Lerp8(bi->hill, bi->skyBottom, 0.55f + 0.12f * i);
        tint.a = (unsigned char)(70 + 35 * i); // far layers fainter than near
        DrawTexture(r->parallax[i], (int)wrapped, (int)oy, tint);
        DrawTexture(r->parallax[i], (int)wrapped + r->w, (int)oy, tint);
    }
}

// Run one fullscreen shader pass: src texture -> dst render target.
// Always draws the RT texture flipped (negative height); doing this on every
// pass keeps a consistent orientation, so the final present is upright.
static void Pass(Shader sh, Texture2D src, RenderTexture2D dst) {
    BeginTextureMode(dst);
    ClearBackground(BLACK);
    BeginShaderMode(sh);
    DrawTextureRec(src, (Rectangle){0, 0, (float)src.width, -(float)src.height},
                   (Vector2){0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void Render_Frame(Renderer *r, const Grid *grid, Camera2D camera, Biome biome) {
    float time = (float)GetTime();
    Shader sh[3] = {r->water, r->heat, r->bloom};
    for (int i = 0; i < 3; i++) {
        int loc = GetShaderLocation(sh[i], "time");
        if (loc != -1) SetShaderValue(sh[i], loc, &time, SHADER_UNIFORM_FLOAT);
    }

    // 1) Background + world into the scene target.
    BeginTextureMode(r->scene);
    ClearBackground(BLACK);
    DrawParallax(r, camera, biome);
    BeginMode2D(camera);
    GridDrawWorld(grid, camera);
    EndMode2D();
    EndTextureMode();

    // 2) Post chain: only the enabled passes run; ping-pong between targets.
    Texture2D cur = r->scene.texture;
    RenderTexture2D *targets[2] = {&r->ping, &r->pong};
    int t = 0;
    if (r->cfg.water) { Pass(r->water, cur, *targets[t]); cur = targets[t]->texture; t ^= 1; }
    if (r->cfg.heat)  { Pass(r->heat,  cur, *targets[t]); cur = targets[t]->texture; t ^= 1; }
    if (r->cfg.bloom) { Pass(r->bloom, cur, *targets[t]); cur = targets[t]->texture; t ^= 1; }

    r->finalTex = cur;
}

void Render_Present(const Renderer *r) {
    DrawTextureRec(r->finalTex,
                   (Rectangle){0, 0, (float)r->finalTex.width, -(float)r->finalTex.height},
                   (Vector2){0, 0}, WHITE);
}

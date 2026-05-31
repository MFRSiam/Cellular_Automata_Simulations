//
// noise.c - Ken Perlin's improved 2D noise with a seeded permutation table.
//
#include "noise.h"
#include <math.h>

static int s_perm[512];

void NoiseInit(unsigned int seed) {
    int p[256];
    for (int i = 0; i < 256; i++) p[i] = i;

    // Fisher-Yates shuffle driven by an xorshift PRNG so it's deterministic.
    unsigned int s = seed ? seed : 1u;
    for (int i = 255; i > 0; i--) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int j = (int)(s % (unsigned)(i + 1));
        int t = p[i]; p[i] = p[j]; p[j] = t;
    }
    for (int i = 0; i < 256; i++) s_perm[i] = s_perm[i + 256] = p[i];
}

static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float Lerp(float a, float b, float t) { return a + t * (b - a); }

static float Grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

float NoisePerlin2(float x, float y) {
    int X = (int)floorf(x) & 255, Y = (int)floorf(y) & 255;
    x -= floorf(x); y -= floorf(y);
    float u = Fade(x), v = Fade(y);
    int A = s_perm[X] + Y, B = s_perm[X + 1] + Y;
    return Lerp(Lerp(Grad(s_perm[A],     x,     y),     Grad(s_perm[B],     x - 1, y),     u),
                Lerp(Grad(s_perm[A + 1], x,     y - 1), Grad(s_perm[B + 1], x - 1, y - 1), u), v);
}

float NoiseFbm(float x, float y, int octaves) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < octaves; o++) {
        sum  += amp * NoisePerlin2(x * freq, y * freq);
        norm += amp;
        freq *= 2.0f;
        amp  *= 0.5f;
    }
    return (sum / norm) * 0.5f + 0.5f;
}

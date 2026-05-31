//
// noise.h - Perlin gradient noise + fBm, sampleable at any world coordinate.
//
#ifndef NOISE_H
#define NOISE_H

// (Re)seed the permutation table. Cheap; call when the world seed changes.
void  NoiseInit(unsigned int seed);

// 2D improved Perlin noise, range ~[-1, 1].
float NoisePerlin2(float x, float y);

// Fractal Brownian motion (summed octaves), normalized to ~[0, 1].
float NoiseFbm(float x, float y, int octaves);

#endif // NOISE_H

//
// config.h - application configuration loaded from an XML file.
//
#ifndef CONFIG_H
#define CONFIG_H

#include "core.h"

typedef struct AppConfig {
    // window
    int  winW, winH, fps;
    char title[64];
    // cave / noise
    float caveScale, caveThreshold, caveMud;
    int   caveOctaves;
    unsigned int caveSeed; // 0 => randomize at startup
    bool  biomes;
    // render
    bool  bloom, water, heat;
    float vignette;
    // audio
    float volMaster, volSfx, volMusic;
    // font
    char  fontPath[256];
    int   fontSize;
    // brush
    int   brushMax, brushDefault;
} AppConfig;

// Sensible defaults, used as a base and when the file/attributes are missing.
AppConfig Config_Default(void);

// Parse the given XML file over a default config. Returns defaults on failure.
AppConfig Config_Load(const char *path);

#endif // CONFIG_H

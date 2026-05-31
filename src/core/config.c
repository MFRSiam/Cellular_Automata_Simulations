//
// config.c - tiny, forgiving XML attribute reader.
//
// This is NOT a general XML parser; it only scans for `<element ...>` tags and
// pulls `attr="value"` pairs out of them, which is all our flat config needs.
//
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AppConfig Config_Default(void) {
    AppConfig c = {
        .winW = 1280, .winH = 720, .fps = 60,
        .caveScale = 0.045f, .caveThreshold = 0.50f, .caveMud = 0.55f,
        .caveOctaves = 4, .caveSeed = 0, .biomes = true,
        .bloom = true, .water = true, .heat = true, .vignette = 0.80f,
        .volMaster = 1.0f, .volSfx = 1.0f, .volMusic = 0.6f,
        .fontSize = 20,
        .brushMax = 40, .brushDefault = 3,
    };
    strcpy(c.title, "Cellular Automata Sims By MFRSiam");
    strcpy(c.fontPath, "assets/fonts/hud.ttf");
    return c;
}

// Return a pointer just past `<name` in `xml`, or NULL.
static const char *FindElement(const char *xml, const char *name) {
    char needle[64];
    snprintf(needle, sizeof needle, "<%s", name);
    const char *p = strstr(xml, needle);
    return p ? p + strlen(needle) : NULL;
}

// Read attribute `attr` from the element text starting at `elem` (up to '>').
// Returns false if not found before the tag closes.
static bool ReadAttr(const char *elem, const char *attr, char *out, int outSz) {
    const char *end = strchr(elem, '>');
    char needle[64];
    snprintf(needle, sizeof needle, "%s=\"", attr);
    const char *p = strstr(elem, needle);
    if (!p || (end && p > end)) return false;
    p += strlen(needle);
    int i = 0;
    while (*p && *p != '"' && i < outSz - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

// In-place readers: only overwrite the target (which already holds the
// default) when the attribute is present. Use strtod/strtol so malformed
// values fail predictably rather than silently like atof/atoi.
static void ReadF(const char *elem, const char *attr, float *v) {
    char buf[64];
    if (elem && ReadAttr(elem, attr, buf, sizeof buf)) *v = (float)strtod(buf, NULL);
}
static void ReadI(const char *elem, const char *attr, int *v) {
    char buf[64];
    if (elem && ReadAttr(elem, attr, buf, sizeof buf)) *v = (int)strtol(buf, NULL, 10);
}
static void ReadB(const char *elem, const char *attr, bool *v) {
    char buf[64];
    if (elem && ReadAttr(elem, attr, buf, sizeof buf))
        *v = (strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0);
}
static void ReadStr(const char *elem, const char *attr, char *out, int outSz) {
    if (elem) ReadAttr(elem, attr, out, outSz);
}

AppConfig Config_Load(const char *path) {
    AppConfig c = Config_Default();
    if (!FileExists(path)) {
        TraceLog(LOG_WARNING, "CONFIG: '%s' not found, using defaults", path);
        return c;
    }
    char *xml = LoadFileText(path);
    if (!xml) return c;

    const char *win   = FindElement(xml, "window");
    const char *cave  = FindElement(xml, "cave");
    const char *rend  = FindElement(xml, "render");
    const char *aud   = FindElement(xml, "audio");
    const char *font  = FindElement(xml, "font");
    const char *brush = FindElement(xml, "brush");

    ReadI(win, "width",  &c.winW);
    ReadI(win, "height", &c.winH);
    ReadI(win, "fps",    &c.fps);
    ReadStr(win, "title", c.title, sizeof c.title);

    ReadF(cave, "scale",        &c.caveScale);
    ReadF(cave, "threshold",    &c.caveThreshold);
    ReadF(cave, "mudThreshold", &c.caveMud);
    ReadI(cave, "octaves",      &c.caveOctaves);
    int seed = (int)c.caveSeed;
    ReadI(cave, "seed", &seed);
    c.caveSeed = (unsigned)seed;
    ReadB(cave, "biomes", &c.biomes);

    ReadB(rend, "bloom",    &c.bloom);
    ReadB(rend, "water",    &c.water);
    ReadB(rend, "heat",     &c.heat);
    ReadF(rend, "vignette", &c.vignette);

    ReadF(aud, "master", &c.volMaster);
    ReadF(aud, "sfx",    &c.volSfx);
    ReadF(aud, "music",  &c.volMusic);

    ReadStr(font, "path", c.fontPath, sizeof c.fontPath);
    ReadI(font, "size", &c.fontSize);

    ReadI(brush, "max",     &c.brushMax);
    ReadI(brush, "default", &c.brushDefault);

    UnloadFileText(xml);
    TraceLog(LOG_INFO, "CONFIG: loaded '%s'", path);
    return c;
}

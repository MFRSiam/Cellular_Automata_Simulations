# Cellular Automata Sandbox

A real-time falling-sand / fluid simulation built with **C17**, **raylib 5.5**, and **CMake**. Paint materials into an infinite procedurally-generated cave system and watch them interact: lava melts rock into obsidian, sand mixed with lava becomes molten glass, fire creeps through oil, water quenches flames into steam.

## Features

### Core Simulation
- **Infinite streaming world** — 3×3 grid of screen-sized chunks simulates around you; pan and zoom freely
- **Persistent edits** — changes you make are saved and restored when you return to that region
- **16+ materials** with realistic physics: wood, sand, water, lava, oil, acid, ice, snow, glass, metal, obsidian, and more
- **Rich interactions**: fire/water/ice reactions, lava quenching, acid containment, combustion propagation
- **Procedural caves** — Perlin noise with biome-driven variation (Open / Rocky / Sandy / Jungle / Cold / Void biomes)

### Visuals
- **Post-processing shaders**: bloom glow (fire, lava, gold), water caustics, heat distortion
- **Parallax background** with biome-specific sky gradients
- **Depth-based darkening** — the deeper you go, the darker it gets
- **Custom font support** (falls back to built-in)

### Developer Features
- **Structure capture tool** (F2) — drag to save hand-made regions, they scatter deterministically in the world
- **Live cave parameter tweaking** — adjust Perlin scale, threshold, octaves, and see changes instantly
- **Settings menu** — toggle effects (bloom, water, heat, biomes) without restarting
- **Menu system** — intro, settings, and in-game pause menu with styled buttons

## Build & Run

### Prerequisites
- **CMake** 3.21+
- **C17** compiler (MSVC, GCC, Clang)
- **Git** (to fetch raylib during build)
- **OpenGL 3.3**+ capable GPU

### Build (Windows Developer PowerShell)
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SandSimulation_Raylib.exe
```

CMake fetches raylib 5.5 automatically; first build takes ~60s to compile raylib itself.

### Build (Linux/macOS)
Same commands; uses your system's default C compiler and OpenGL.

## Controls

### Materials (In-Game)
| Key | Material |
|---|---|
| `1`–`9` | Sand, Water, Wood, Oil, Acid, Snow, Fire, Gas, Vapor |
| `L` | Lava |
| `R` | Rock / `M` Mud / `V` Glass / `B` Metal / `N` Obsidian |
| `0` | Eraser |

### Interaction
- **Left-click + drag** — paint the selected material
- **Right-click** — erase
- **Mouse wheel** — adjust brush size
- **WASD / arrows** — pan the world
- **Q / E** — zoom out / zoom in (0.34× to 16× magnification)
- **Middle-click + drag** — pan smoothly

### Editing
- **F2** — enter structure capture mode (drag a rectangle, release to save)
- **C** — clear the entire grid (except persisted regions)
- **[** / **]** — decrease / increase Perlin noise scale (zoom out / in on detail)
- **,** / **.** — decrease / increase cave openness threshold
- **;** / **'** — decrease / increase noise octaves (roughness)
- **G** — generate a new random seed / world

### UI
- **ESC** — pause / resume (pause menu has Resume, New Seed, Clear, Main Menu buttons)

## Material Interactions

| Interaction | Result |
|---|---|
| Fire + Water | Water → Vapor (fire dies to smoke) |
| Fire + Ice | Ice → Water |
| Lava + Water | Lava → Basalt (black stone), Water → Vapor |
| Lava + Ice | Ice melts to water |
| Lava (air-cooled) | → Obsidian (dark glass) |
| Lava + Rock/Mud/Sandstone | Slowly melts them back into lava |
| Lava + Sand | Sand → Molten Glass |
| Molten Glass + Water/Air | → Solid Glass |
| Acid + Solids/Powders | Dissolves them (unless acid-proof) |
| Acid + Glass/Metal/Obsidian | **Contained** (acid-proof materials hold it) |
| Fire + Oil/Gas | Catch quickly and burn up fast |
| Fire + Wood/Moss | Smoulder slowly |
| Snow + Heat | Melts to water |

## Configuration

Edit `config/config.xml` to customize:
- **Window size, FPS target**
- **Cave generation**: scale (frequency), threshold (openness), mud density, octaves (roughness), biome on/off
- **Post-effects**: bloom, water caustics, heat distortion, vignette
- **Audio**: volume levels for master/SFX/music
- **HUD font**: path to a TTF file (falls back to built-in if missing)
- **Brush**: max size, default size

Example:
```xml
<cave scale="0.045" threshold="0.50" octaves="4" biomes="true"/>
<render bloom="true" water="true" heat="true"/>
```

## Audio

Place audio files in `assets/audio/`:
- `place.wav` — painting sound
- `ignite.wav` — fire/combustion
- `ui.wav` — menu clicks
- `music.ogg` — background loop

All files are optional; the app runs silent if they're missing.

## Shaders

Fragment shaders in `shaders/`:
- **bloom.fs** — final glow pass (emissive-only, raises brightness)
- **water.fs** — caustics + refraction for water-like pixels
- **heat.fs** — rising shimmer distortion for fire/lava

## Architecture

```
src/
  core.h           — shared constants & macros
  config.{h,c}     — XML config parsing (strtol/strtod for safety)
  noise.{h,c}      — Ken Perlin 2D gradient noise + fBm
  materials.{h,c}  — material catalogue & properties (16 cell types)
  biome.{h,c}      — biome classification & per-biome parameters
  grid.{h,c}       — cellular automaton + persistent hash-map store
  structure.{h,c}  — deterministic structure placement (dev tool)
  render.{h,c}     — background parallax + post-processing shader pipeline
  audio.{h,c}      — optional audio playback (graceful if no files)
  hud.{h,c}        — styled menu, pause, settings, in-game overlay
  main.c           — app loop: menu → game flow, chunked simulation
```

## Design Notes

- **Chunked simulation**: The world window covers a 3×3 grid of screen-sized chunks, so all 8 neighbors around you simulate in real-time. Off-screen sand keeps falling, off-screen fire keeps spreading.
- **Persistence**: A hash-map remembers edited cells by absolute world coordinate. Regens only clear the store (New Game); returning to a region restores your edits.
- **Modern C**: C17 features (designated initializers, inline functions, `<stdbool.h>`, `<stdint.h>`). No C++ or external libraries beyond raylib.
- **Data-driven**: Materials and biomes are tables; reactions are pure functions. Changing a single value in `materials.c` affects the whole world.
- **Deterministic placement**: Structures are scattered by a pure function of region coords + world seed, so they reappear on regen in the same spots.

## Biomes

Each biome has a unique Perlin scale, roughness, wall materials, and background palette:

| Biome | Wall | Vibe | Noise |
|---|---|---|---|
| Open | Rock + Mud veins | Spacious airy caves | Smooth, low-frequency |
| Rocky | Rock + Mud veins | Dense, craggy | High-frequency detail |
| Sandy | Sand surface over Sandstone | Desert-like | Medium, with surface sand |
| Jungle | Mud + Moss | Humid, organic | Organic, mossy accents |
| Cold | Ice + Rock | Frozen, brittle | Smooth, icy blues |
| Void | Rock (sparse, rare) | Vast dark chasm | Very low-frequency, massive empty spaces |

## Known Limitations & Future Ideas

- **Persistence memory**: currently stores every edited cell (grows with exploration). Could optimize to store only diffs.
- **Structure format**: simple ASCII grid. Could add 3D structures or more complex patterns.
- **Audio**: no music streaming yet; all files must be small WAV/OGG.
- **Multiplayer**: world is single-player only (no network code).

## Credits

Built with:
- **raylib 5.5** — 2D graphics and input
- **CMake** — cross-platform build
- **Ken Perlin's noise** — procedural caves
- **GLSL 330** — post-processing shaders

---

**Happy digging!** 🪨💧🔥

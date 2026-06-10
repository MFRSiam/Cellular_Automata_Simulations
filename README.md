# Cellular Automata Sandbox

A real-time falling-sand / fluid simulation built with **C17**, **raylib 5.5**, and **CMake**. Paint materials into an infinite procedurally-generated cave system and watch them interact: lava melts rock into obsidian, sand mixed with lava becomes molten glass, fire creeps through oil, water quenches flames into steam.

## Features

### Core Simulation
- **Infinite streaming world** — 3×3 grid of screen-sized chunks simulates around you; pan and zoom freely
- **Persistent edits** — changes you make are saved and restored when you return to that region
- **35+ materials** with realistic physics: wood, sand, water, lava, oil, acid, ice, snow, glass, metal, obsidian, three gases, grass, vines, copper, crystal, falling gold, salt, ash, coal, gunpowder, electric sparks, mercury, wax, and more
- **Rich interactions**: fire/water/ice reactions, lava quenching, acid containment, combustion, freezing, plant growth, clay firing, acid dilution, explosions, electricity, dissolving, melting/setting
- **Three gases** — flammable, inert, and corrosive (acidic) — plus condensing vapour
- **Electricity** — sparks arc along conductors (metal, copper, gold, water, mercury), ignite fuel, detonate gunpowder, and flash-boil water

### World Generation
- **Noita-style silhouettes** — the base field is deliberately smooth (low frequency, 2 octaves, zero per-cell roughness); all the character comes from a **two-stage domain warp** (a big sweep that bends whole formations + a smaller swirl that curls the edges), producing flowing rock tongues and overhangs with clean boundaries. Caverns are anisotropic (wider than tall → walkable floors) and **worm-tunnel ridges** at two scales link them
- **Characterful spikes** — stalactites speak each biome's language: rock cones, **icicles** in cold, **coral fingers** in the reef, **obsidian fangs** in the void
- **Depth strata** — the band around y=0 is airy and open; the world grows denser with depth and fades into **basalt/obsidian** bedrock; ores are layered too (coal shallow → copper mid → gold deep)
- **Ore veins** — elongated ridged streaks, not blobs; cut a deep gold vein and the gold **pours out** (it's a heavy powder)
- **Top-soil** — any wall under open air gets a biome cover: mud (grass roots in it), dunes of sand, or **snow-capped ice** in cold biomes
- **Stalactites & stalagmites** — long contiguous cones (2–7 cells) on ~1 in 4 columns
- **Anchored liquid pockets** — one lens-shaped pocket per ~96-cell region (30% chance), wider than tall like real ponds, with an organically wobbled but **guaranteed-sealed** shell; contents follow depth: surface ponds → mid water/oil/acid → deep **lava lakes** and **sealed gold treasure pockets**
- **Living world** — grass **spreads across mud surfaces** (and the fresh mud worms leave), vines drape jungle ceilings, moss creeps over damp soil near water
- **Critters** (pixel-art) — **frogs** leap through jungle biomes actively hunting **flies**; flies **multiply** over time but **disperse** so they don't clump in pits; **worms** burrow through rocky biomes, slowly **gnawing each rock into mud** over time (which grass then colonises). Kill any critter with an element (fire/lava/acid/spark…) and it spills **blood** — which then attracts more flies (carrion → predators → more blood)
- **Out-of-this-world Void biome** — dark obsidian shot through with shiny copper veins and glowing crystals
- **Rare gold chambers** scattered deep in the rock (gold is heavy and falls when exposed)
- **Oil reservoirs** — huge oil pools encased in a wooden shell scattered throughout; breach or torch the wood and they gush/ignite
- **Living coral** slowly encrusts sand underwater
- **Procedural biomes** with softened, organic borders (Open / Rocky / Sandy / Jungle / Cold / Void)

### Visuals
- **Post-processing shaders**: bloom glow (fire, lava, gold), water caustics, heat distortion
- **Parallax background** with biome-specific sky gradients
- **Depth-based darkening** — the deeper you go, the darker it gets
- **Custom font support** (falls back to built-in)

### Developer Features
- **Structure Manager** — a dedicated menu scene (built with raygui) to browse saved structures as thumbnails, **delete** them, and create new ones
- **Structure Editor** — a blank canvas with the live simulation running and a full tool box: **Brush, straight Line, Rectangle, Circle (outline or filled), flood Fill, and an eyedropper Pick tool**, with drag previews on the canvas. Materials are chosen from a **swatch picker** in the side panel (same palette as in-game), with a brush-size slider, live stats (total pixels + top materials), and play/pause/clear/save controls
- **Structure capture tool** (F2) — in-world: drag to save hand-made regions, they scatter deterministically in the world
- **Live cave parameter tweaking** — adjust Perlin scale, threshold, octaves, and see changes instantly
- **Settings menu** — toggle effects (bloom, water, heat, biomes) without restarting
- **Menu system** — intro, structures, settings, and in-game pause menu with styled buttons

### Performance
- **Multithreaded simulation** — the cellular automaton runs on a background worker thread while the main thread renders, so heavy scenes stay responsive
- **Modern OpenGL 4.3** backend with GLSL 4.30 post-processing shaders

## Build & Run

### Prerequisites
- **CMake** 3.21+
- **C17** compiler (MSVC, GCC, Clang)
- **Git** (to fetch raylib + raygui during build)
- **OpenGL 4.3**+ capable GPU

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
Materials are chosen from the **on-screen palette panel** (right side) — click a colour
swatch to select it; the **Eraser** swatch (or right-click) clears cells. A slider on the
panel sets brush size (mouse wheel also works). The HUD shows the selected material and
your **position relative to the world origin (0,0)**.

### Interaction
- **Left-click + drag** — paint the selected material
- **Right-click** — erase
- **Mouse wheel** — adjust brush size
- **WASD / arrows** — pan the world
- **Q / E** — zoom out / zoom in (0.18× to 16× magnification)
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
| Lava + Rock/Mud/Sandstone | Erodes them back into lava **very slowly** (minutes, not seconds) |
| Lava + Sand/Glass/Wax/Ice | Melts each at its own rate — heat soaks in over time, nothing converts instantly |
| Lava + Sand | Sand → Molten Glass |
| Molten Glass + Water/Air | → Solid Glass |
| Acid + Solids/Powders | Dissolves them — and the reaction **consumes the acid** (each cell has potency; spent acid fizzles out, sometimes as corrosive gas) |
| Acid + Water | **Dilutes gradually** — adjacent water saps potency, the acid visibly pales, and only fully-drained acid becomes water |
| Acid + Glass/Metal/Obsidian | **Contained** (acid-proof materials hold it) |
| Fire + Oil/Gas | Catch quickly and burn up fast |
| Fire + Wood/Moss | Smoulder slowly |
| Snow + Heat | Melts to water |
| Water surrounded by Ice/Snow (no heat) | **Freezes** to ice (phase change) |
| Moss + adjacent Water | **Grows** onto neighbouring mud/sand (plants colonise damp soil) |
| Mud + Fire/Lava | **Bakes** into sandstone (fired clay → ceramic) |
| Gunpowder + Fire/Lava/Spark | **Explodes** — fireball that ignites and blasts soft matter |
| Spark + conductor (metal/copper/gold/water/mercury) | **Arcs** along it, fading over distance |
| Spark + Oil/Gas/Gunpowder | Ignites / detonates |
| Spark + Water | Flash-boils to vapour |
| Salt + Ice/Snow | **Melts** it to water (freezing-point depression) |
| Salt / Ash + Water | Salt **dissolves**; ash becomes **mud** |
| Mercury + Gold | Slowly **dissolves** gold into amalgam |
| Wax + Heat | **Melts** to molten wax, which **flows then re-sets** |
| Glass + Lava | **Re-melts** to molten glass |
| Mercury + Fire/Lava | Boils into **toxic acid-gas** fumes |
| Spark + Crystal | Crystals **conduct** electricity too |
| Fire burning out | Leaves **ash**; coal burns long and hot |
| Blood (from dead critters) | **Attracts flies** (carrion) → which draws frogs → more blood |

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

The sources are grouped into modules by responsibility:

```
src/
  main.c             — app loop: menu/structure/game flow, input, threading
  core/              — shared definitions & platform infrastructure
    core.h           — shared constants & macros
    config.{h,c}     — XML config parsing (strtol/strtod for safety)
    thread.{h,c}     — cross-platform thread + mutex wrapper (Win32 / pthreads)
    audio.{h,c}      — optional audio playback (graceful if no files)
  world/             — simulation & procedural generation
    materials.{h,c}  — material catalogue & properties
    noise.{h,c}      — Ken Perlin 2D gradient noise + fBm
    biome.{h,c}      — biome classification & per-biome parameters
    grid.{h,c}       — cellular automaton + persistent hash-map store
    structure.{h,c}  — deterministic structure placement
    sim.{h,c}        — background simulation worker thread
  render/
    render.{h,c}     — background parallax + post-processing shader pipeline
  ui/                — raygui-based interface
    hud.{h,c}        — styled menu, pause, settings, in-game overlay
    strmgr.{h,c}     — Structure Manager scene (browse/create, thumbnails)
    raygui_impl.c    — raygui implementation translation unit
```

### Threading model

The simulation is **fully multithreaded** (`world/sim.c`):
- A **coordinator thread** paces steps at the target rate, sleeping only the
  *remainder* of each step's time budget.
- A persistent **worker pool** (cores − 2 helpers; the coordinator and, during
  regeneration, the main thread also participate) executes the step as column
  strips in **two checkerboard phases** — even strips in parallel, then odd —
  so concurrently-processed strips are never adjacent and cell interactions
  (reach ≤ 7 cells, strips ≥ 24 wide) cannot race.
- **World generation runs on the same pool** (row bands, pure functions of
  seed + coordinates, so output is deterministic regardless of thread count).
- Simulation code uses **thread-local RNG** (xorshift32) — raylib's global RNG
  is only touched from the main thread.
- A mutex guards the grid: the main thread paints/streams/snapshots under the
  lock, then drawing + shaders run unlocked, overlapping the next sim step.
  If the coordinator fails to start, the sim falls back to the main thread.

### Performance architecture

- **Sleeping tiles** — the buffer is split into 32×32 tiles; fully settled tiles
  are skipped by the simulation. Any cell change wakes its 3×3 tile
  neighbourhood, and a small random set of tiles is woken each step so ambient
  life (grass, moss, coral, lava cooling) still ticks. The HUD shows the
  percentage of tiles awake (`sim N%`).
- **One-texture world rendering** — every frame the cell snapshot is converted
  to a one-texel-per-cell RGBA buffer and uploaded as a single texture, drawn
  as one scaled quad. Only the camera-visible rect is recomputed, and at far
  zoom (cells sub-pixel) a flat-colour fast path skips the animated effects.
  All compositing, water/heat/bloom post-processing, and scaling run on the
  GPU (GLSL 4.30).
- **Two-pass generation** — `GenRegion` samples biome + openness once per cell
  into cached arrays (pass 1), then decides materials with all vertical probes
  (stalactites, surfaces, plant anchors) as array reads (pass 2). Streaming
  generates only the newly exposed strips, and the window origin moves in
  8-cell steps so strips arrive in batches that amortise the generation margin.
- **World-anchored speckle** — per-cell colour noise is keyed on world
  coordinates, so terrain texture stays glued to the terrain while panning
  instead of "swimming".

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
| Void | Obsidian + Copper + Crystal | Vast dark cosmic chasm | Very low-frequency, massive empty spaces |
| Coral | Coral + Sand | Colourful flooded reef | Dense, full of contained water pockets |

## Known Limitations & Future Ideas

- **Persistence memory**: currently stores every edited cell (grows with exploration). Could optimize to store only diffs.
- **Structure format**: simple ASCII grid. Could add 3D structures or more complex patterns.
- **Audio**: no music streaming yet; all files must be small WAV/OGG.
- **Multiplayer**: world is single-player only (no network code).

## Credits

Built with:
- **raylib 5.5** — 2D graphics and input
- **raygui 4.0** — immediate-mode UI (Structure Manager)
- **CMake** — cross-platform build
- **Ken Perlin's noise** — procedural caves
- **GLSL 4.30** — post-processing shaders

---

**Happy digging!** 🪨💧🔥

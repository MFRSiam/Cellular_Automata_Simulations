# Audio pack

The audio system looks for these files here (all optional — missing ones are
skipped silently):

| File         | Played when            |
|--------------|------------------------|
| `place.wav`  | painting material      |
| `ignite.wav` | fire/lava ignites      |
| `ui.wav`     | UI button click        |
| `music.ogg`  | looping background music |

Supported formats: `.wav`, `.ogg`, `.mp3`. Add files and restart — no code
changes needed (see `src/audio.c`, the `SOUND_FILES` table).

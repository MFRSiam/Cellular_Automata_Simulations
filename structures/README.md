# Structures (dev tool)

Each `*.txt` is a hand-made structure scattered into the world by
`StructureSampleAt` (deterministic per seed, so they reappear on regen).

Format:
```
<width> <height>
<row 0: width cell-ids separated by spaces>
...
```
Cell ids match the `Cell` enum in `src/materials.h` (0=empty/transparent,
3=wood, 12=rock, 17=gold, ...). A `0` means "leave the existing terrain".

To make one in-game: press **F2**, drag a rectangle around something you built,
release. It is saved here and placed in the world on the next regenerate.

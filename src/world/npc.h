//
// npc.h - tiny critters that live on top of the cellular grid.
//
// Frogs hop around jungle biomes hunting flies; flies buzz through the air.
// They are agents (float positions + simple AI), not cells. When a dangerous
// material (fire, lava, acid, spark...) touches one it dies and spills blood.
//
// Npc_Update reads and writes the grid, so it must be called under the sim
// lock (like painting/streaming). Npc_Draw renders sprites in world space and
// must be called inside BeginMode2D(camera).
//
#ifndef NPC_H
#define NPC_H

#include "grid.h"

void Npc_Reset(void);                 // clear all critters (new world)
void Npc_Update(Grid *g, float dt);   // AI + physics + spawning + death
void Npc_Draw(void);                  // draw sprites (inside BeginMode2D)

#endif // NPC_H

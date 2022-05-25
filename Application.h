//
// Created by mfrfo on 3/29/2022.
//

#ifndef SANDSIMULATION_RAYLIB_APPLICATION_H
#define SANDSIMULATION_RAYLIB_APPLICATION_H
#include <raylib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// Every Actor in the Scene is a CELL
enum CellType{
    Wood,
    Sand,
    Water,
    Floor,
    Null
}typedef CellType;


struct Cell{
    Vector2 Pos;
    CellType Type;
    float velocity;
}typedef Cell;




/*! @brief This Crates a Floor in the Array consisting of f as a indicator of being floor.
 * @param ptr The Cells that will be modified
 * @param x The width of the play area
 * @param y The height of the play area
 */
void SetFloor(Cell **ptr,int x,int y);

void SpawnSandAtMouseLocation(Cell **ptr,Vector2 *mousePos,float resulution);
void SpawnWoodAtMouseLocation(Cell **ptr,Vector2 *mousePos,float resulution);
void SpawnWaterAtMouseLocation(Cell **ptr,Vector2 *mousePos, float resulution);

/*! @brief Creates a Debug file in the dir named dump.txt for some dumps
 * @param ptr The Array data which will be dumped
 * @param playHeight Play Area Y
 * @param playWidth Play Area X
 */
void DumpFile(Cell **ptr,int playHeight,int playWidth);


void ApplicationDraw(Cell **ptr,Vector2 size,int playHeight,int playWidth);


void UpdateMap(Cell **ptr,int height_y,int width_x);

void InitiateCells(Cell **ptr,int playHeight,int playWidth,int screenHeight, float resulotion);
void ResetMap(Cell **ptr,int playHeight,int playWidth);

void checkMode(char *mode);

void SpawnStuff(Cell **ptr,Vector2 *mousePos,const char *mode,int screenWidth,int screenHeight, float resulution);

void SandLogic(Cell **ptr,int i, int j,int height_y,int width_x);
void WaterLogic(Cell **ptr,int i, int j,int height_y,int width_x);

#endif //SANDSIMULATION_RAYLIB_APPLICATION_H

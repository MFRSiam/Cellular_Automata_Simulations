//
// Created by mfrfo on 3/29/2022.
//

#ifndef SANDSIMULATION_RAYLIB_APPLICATION_H
#define SANDSIMULATION_RAYLIB_APPLICATION_H
#include <raylib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

typedef enum{
    EMPTY,
    WOOD,
    SAND,
    WATER
}G_Materials;

typedef struct{
    G_Materials g_m;
    int pos_x;
    int pos_y;
}Nodes;

void setNodes(Nodes *arr, int l_rows, int l_cols, int size);
void drawNodes(Nodes *arr, int size, int elements);


#endif //SANDSIMULATION_RAYLIB_APPLICATION_H

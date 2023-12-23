#include <raylib.h>
#include <stdlib.h>
#include <math.h>
#include "Application.h"




int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "Cellular Automata");
    SetTargetFPS(60);

    int gridSize = 32;
    int rows = screenHeight / gridSize;
    int columns = screenWidth / gridSize;
    int elements = rows * columns;
    Nodes *arr = (Nodes*) malloc(elements * sizeof(Nodes));


    while (!WindowShouldClose())
    {
        BeginDrawing();

        EndDrawing();
    }
    CloseWindow();
    free(arr);

    return 0;
}


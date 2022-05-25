#include <raylib.h>
#include <stdlib.h>
#include <math.h>
#include "Application.h"



int main() {
//    const int screenWidth = 800;
//    const int screenHeight = 450;

    const int screenWidth = 1000;
    const int screenHeight = 800;
    const float resolution = 8;
    int playHeight = (int)((float)screenHeight / resolution);
    int playWidth = (int)((float)screenWidth / resolution);

    InitWindow(screenWidth, screenHeight, "My Basic Window");
    SetTargetFPS(144);

    Cell** cells = (Cell**) calloc(playHeight,sizeof(Cell**));
    for(int i=0;i<playHeight;i++){
        cells[i] = (Cell*) calloc(playWidth,sizeof(Cell));
    }

    InitiateCells(cells, playHeight, playWidth, screenHeight, resolution);
    SetFloor(cells,playWidth,playHeight);

    Vector2 mousePosition;
    Vector2 size = {resolution, resolution};
    char mode = 's';
    while (!WindowShouldClose())
    {

        mousePosition = GetMousePosition();
        checkMode(&mode);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)){

            SpawnStuff(cells, &mousePosition, &mode, screenWidth, screenHeight, resolution);
        }
        else if (IsKeyDown(KEY_R)){
            ResetMap(cells,playHeight,playWidth);
        }

        double time = GetTime();

        //printf("%f ,",time);
        if(time>1.0f){
            UpdateMap(cells,playHeight,playWidth);
            time = 0.0f;
        }
        BeginDrawing();
        ClearBackground(RAYWHITE);
        ApplicationDraw(cells,size,playHeight,playWidth);
        EndDrawing();

    }
    DumpFile(cells,playHeight,playWidth);
    CloseWindow();
    free(cells);
    return EXIT_SUCCESS;
}


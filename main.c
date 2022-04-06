#include <raylib.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "Application.h"



int main() {
//    const int screenWidth = 800;
//    const int screenHeight = 450;
    const int screenWidth = 1000;
    const int screenHeight = 800;
    int playHeight = screenHeight/10;
    int playWidth = screenWidth/10;

    InitWindow(screenWidth, screenHeight, "My Basic Window");
    SetTargetFPS(144);

    Cell** players = (Cell**) calloc(playHeight,sizeof(Cell**));
    for(int i=0;i<playHeight;i++){
        players[i] = (Cell*) calloc(playWidth,sizeof(Cell));
    }

    float box_x=0.0f,box_y=0.0f;
    for(int i=0;i<playHeight;i++){
        for(int j=0;j<playWidth;j++){
            players[i][j].propertie = 'n';
            players[i][j].Pos.x = box_x;
            players[i][j].Pos.y = box_y;
            box_x += 10.0f;

        }
        box_x = 0.0f;
        box_y += 10.0f;
        if(box_y>(float)screenHeight){
            box_y = 0.0f;
        }
    }
    SetFloor(players,playWidth,playHeight);

    Vector2 mousePosition;
    Vector2 size = {10,10};
    char mode = 's';
    while (!WindowShouldClose())
    {

        mousePosition = GetMousePosition();
        checkMode(&mode);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)){

            SpawnStuff(players,&mousePosition,&mode, screenWidth,screenHeight);
        }
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)){

        }



        double time = GetTime();
//        printf("%f",time);
        if(time>1.0f){
            UpdateMap(players,playHeight,playWidth);
            time = 0.0f;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        ApplicationDraw(players,size,playHeight,playWidth);
        EndDrawing();

    }
    DumpFile(players,playHeight,playWidth);
    CloseWindow();
    free(players);
    return EXIT_SUCCESS;
}


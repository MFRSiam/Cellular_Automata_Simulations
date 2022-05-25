//
// Created by mfrfo on 3/29/2022.
//

#include "Application.h"


void SetFloor(Cell **ptr,int x,int y){
    int flooring = y-2;
    for(int i=flooring;i<y;i++){
        for(int j=0;j<x;j++){
            ptr[i][j].Type = Floor;
        }
    }
}

void SpawnSandAtMouseLocation(Cell **ptr,Vector2 *mousePos, float resulution){
//    printf("Mouse Pos x: %f and y: %f \n",mousePos->x,mousePos->y);
    float a_x = mousePos->x/resulution;
    float a_y = mousePos->y/resulution;
    int x = (int)ceilf(a_x);
    int y = (int)ceilf(a_y) - 1;
    if(ptr[y][x].Type != Sand){
        ptr[y][x].Type = Sand;
    }
}

void SpawnWoodAtMouseLocation(Cell **ptr,Vector2 *mousePos, float resulution){
    float a_x = mousePos->x/resulution;
    float a_y = mousePos->y/resulution;
    int x = (int)ceilf(a_x);
    int y = (int)ceilf(a_y) - 1;
    if(ptr[y][x].Type != Wood){
        ptr[y][x].Type = Wood;
    }
}

void SpawnWaterAtMouseLocation(Cell **ptr,Vector2 *mousePos, float resulution){
    float a_x = mousePos->x/resulution;
    float a_y = mousePos->y/resulution;
    int x = (int)ceilf(a_x);
    int y = (int)ceilf(a_y) - 1;
    if(ptr[y][x].Type != Water){
        ptr[y][x].Type = Water;
        ptr[y][x].velocity = 1.0f;
    }
}

void DumpFile(Cell **ptr,int playHeight,int playWidth){
    FILE  *myFile;
    myFile = fopen("dump.txt","w+");
    fprintf(myFile,"Debug Dumb File\n");

    for(int i=0;i<playHeight;i++){
        for(int j=0;j<playWidth;j++){
//            fprintf(myFile,"%.1f::%.1f ",ptr[i*j].Pos.x,ptr[i*j].Pos.y);
//            fprintf(myFile,"%c and PosX: %0.1f Pox Y: %0.1f . ",ptr[i][j].propertie,ptr[i][j].Pos.x,ptr[i][j].Pos.y);
            fprintf(myFile,"%c",ptr[i][j].Type);
            if(ptr[i][j].Type == Sand){

            }
        }
        fprintf(myFile,"\n");
    }

    fclose(myFile);
}


void ApplicationDraw(Cell **ptr,Vector2 size,int playHeight,int playWidth){
//    int counter = 0;
    for(int i=0;i<playHeight;i++){
        for(int j=0;j<playWidth;j++){
            switch (ptr[i][j].Type) {
                case Floor:
//                    counter++;
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,BLACK);
                    break;
                case Sand:
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,BROWN);
                    break;
                case Wood:
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,GREEN);
                    break;
                case Water:
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,BLUE);
                    break;
            }
        }
    }
//    printf("Counter: %d",counter);
}


void UpdateMap(Cell **ptr,int height_y,int width_x){
    for(int i=height_y-1;i>=0;i--){
        for(int j=width_x-1;j>=0;j--){
            switch (ptr[i][j].Type) {
                case Sand:
                    SandLogic(ptr,i,j,height_y,width_x);
                    break;
                case Water:
                    WaterLogic(ptr,i,j,height_y,width_x);
                    break;
                case Wood:
                    break;
                case Floor:
                    break;
                case Null:
                    break;
            }
        }
    }
}

void WaterLogic(Cell **ptr,int i, int j,int height_y,int width_x){
    if((i+1) >= height_y || (j+1) >= width_x || (i-1)<0 || (j-1)<0){
        return;
    }
    if(ptr[i+1][j].Type == Null){
        ptr[i][j].Type = Null;
        ptr[i+1][j].Type = Water;
    }else if(ptr[i+1][j+1].Type==Null){
        ptr[i][j].Type = Null;
        ptr[i+1][j+1].Type = Water;
    }else if(ptr[i+1][j-1].Type ==Null){
        ptr[i][j].Type = Null;
        ptr[i][j-1].Type = Water;
    }else if(ptr[i][j+1].Type == Null){
        ptr[i][j].Type = Null;
        ptr[i][j+1].Type =Water;
    }else if(ptr[i][j-1].Type == Null){
        ptr[i][j].Type = Null;
        ptr[i][j-1].Type =Water;
    }
}

void SandLogic(Cell **ptr,int i, int j,int height_y,int width_x){
    if((i+1) >= height_y || (j+1) >= width_x ){
        return;
    }
    if(ptr[i+1][j].Type== Floor || ptr[i+1][j].Type== Wood){
        return;
    }
    if(ptr[i+1][j].Type == Null){
        ptr[i][j].Type = Null;
        ptr[i+1][j].Type = Sand;
    }else if(ptr[i+1][j-1].Type == Null){
        ptr[i][j].Type = Null;
        ptr[i+1][j-1].Type = Sand;
    }else if(ptr[i+1][j+1].Type == Null){
        ptr[i][j].Type = Null;
        ptr[i+1][j+1].Type = Sand;
    }
}





void checkMode(char *mode){
    if(IsKeyDown(KEY_S)){
        *mode = Sand;
    }else if(IsKeyDown(KEY_W)){
        *mode = Wood;
    }else if(IsKeyDown(KEY_L)){
        *mode = Water;
    }
}

void SpawnStuff(Cell **ptr,Vector2 *mousePos,const char *mode,int screenWidth,int screenHeight,float resulution){
    if(mousePos->x < 0 || mousePos->y<0 || mousePos->x >= (float)screenWidth || mousePos->y >= (float)screenHeight){
        return;
    }
    if(*mode == Sand){
        SpawnSandAtMouseLocation(ptr,mousePos,resulution);
    }else if(*mode == Wood){
        SpawnWoodAtMouseLocation(ptr,mousePos,resulution);
    }else if(*mode == Water){
        SpawnWaterAtMouseLocation(ptr,mousePos,resulution);
    }
}

void InitiateCells(Cell **players,int playHeight,int playWidth,int screenHeight,float resulotion){
    float box_x=0.0f,box_y=0.0f;
    for(int i=0;i<playHeight;i++){
        for(int j=0;j<playWidth;j++){
            players[i][j].Type = Null;
            players[i][j].Pos.x = box_x;
            players[i][j].Pos.y = box_y;
            box_x += resulotion;

        }
        box_x = 0.0f;
        box_y += resulotion;
        if(box_y>(float)screenHeight){
            box_y = 0.0f;
        }
    }
}

void ResetMap(Cell **ptr,int playHeight,int playWidth){
    for(int i=0;i<playHeight;i++){
        for(int j=0;j<playWidth;j++){
            ptr[i][j].Type = Null;
        }
    }
    SetFloor(ptr,playWidth,playHeight);
}
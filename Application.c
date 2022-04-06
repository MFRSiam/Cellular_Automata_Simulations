//
// Created by mfrfo on 3/29/2022.
//

#include "Application.h"


void SetFloor(Cell **ptr,int x,int y){
    int flooring = y-2;
    for(int i=flooring;i<y;i++){
        for(int j=0;j<x;j++){
            ptr[i][j].propertie = 'f';
        }
    }
}

void SpawnSandAtMouseLocation(Cell **ptr,Vector2 *mousePos){
//    printf("Mouse Pos x: %f and y: %f \n",mousePos->x,mousePos->y);
    float a_x = mousePos->x/10.0f;
    float a_y = mousePos->y/10.0f;
    int x = (int)ceilf(a_x);
    int y = (int)ceilf(a_y) - 1;
    if(ptr[y][x].propertie != 's'){
        ptr[y][x].propertie = 's';
    }
}

void SpawnWoodAtMouseLocation(Cell **ptr,Vector2 *mousePos){
    float a_x = mousePos->x/10.0f;
    float a_y = mousePos->y/10.0f;
    int x = (int)ceilf(a_x);
    int y = (int)ceilf(a_y);
    if(ptr[y][x].propertie != 'w'){
        ptr[y][x].propertie = 'w';
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
            fprintf(myFile,"%c",ptr[i][j].propertie);
            if(ptr[i][j].propertie == 's'){

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
            switch (ptr[i][j].propertie) {
                case 'f':
//                    counter++;
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,BLACK);
                    break;
                case 's':
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,BROWN);
                    break;
                case 'w':
                    DrawRectangle(ptr[i][j].Pos.x,ptr[i][j].Pos.y,size.x,size.y,GREEN);
                    break;
            }
        }
    }
//    printf("Counter: %d",counter);
}


void UpdateMap(Cell **ptr,int height_y,int width_x){
    for(int i=height_y-1;i>=0;i--){
        for(int j=width_x-1;j>=0;j--){
            switch (ptr[i][j].propertie) {
                case 's':
                    if((i+1) >= height_y || (j+1) >= width_x ){
                        break;
                    }
                    if(ptr[i+1][j].propertie=='f' || ptr[i+1][j].propertie=='w'){
                        break;
                    }
                    if(ptr[i+1][j].propertie == 'n'){
                        ptr[i][j].propertie = 'n';
                        ptr[i+1][j].propertie = 's';
                    }else if(ptr[i+1][j-1].propertie == 'n'){
                        ptr[i][j].propertie = 'n';
                        ptr[i+1][j-1].propertie = 's';
                    }else if(ptr[i+1][j+1].propertie == 'n'){
                        ptr[i][j].propertie = 'n';
                        ptr[i+1][j+1].propertie = 's';
                    }
                    break;


            }
        }
    }
}

void checkMode(char *mode){
    if(IsKeyDown(KEY_S)){
        *mode = 's';
    }if(IsKeyDown(KEY_W)){
        *mode = 'w';
    }
}

void SpawnStuff(Cell **ptr,Vector2 *mousePos,const char *mode,int screenWidth,int screenHeight){
    if(mousePos->x < 0 || mousePos->y<0 || mousePos->x > (float)screenWidth || mousePos->y > (float)screenHeight){
        return;
    }
    if(*mode == 's'){
        SpawnSandAtMouseLocation(ptr,mousePos);
    }else if(*mode == 'w'){
        SpawnWoodAtMouseLocation(ptr,mousePos);
    }
}
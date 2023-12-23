//
// Created by mfrfo on 3/29/2022.
//

#include "Application.h"

void setNodes(Nodes *arr, int l_rows, int l_cols, int size){
    for(int i=1; i<l_rows; i++){
        for(int j=1;j<l_cols;j++){
            arr[i*j].g_m = EMPTY;
            arr[i*j].pos_y += size;
        }
        arr
    }
}

void drawNodes(Nodes *arr, int size, int elements){
    for(int i=0; i < elements ; i++){

    }
}
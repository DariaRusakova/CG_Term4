#pragma once
#include <DirectXMath.h>
#include "AABB.h"

struct QuadNode {
    AABB  bounds;
    int   level = 0;

    int   tilesStartX = 0;
    int   tilesStartY = 0;
    int   tilesPerSide = 0;

    QuadNode* children[4] = {};  

    int meshIndex = -1;  

    float radius = 0.0f;                              
    DirectX::XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };  
    bool currentlySplit = false;                     

    bool IsLeaf() const { return children[0] == nullptr; }
};
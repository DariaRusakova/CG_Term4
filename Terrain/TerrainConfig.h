#pragma once

struct TerrainConfig {
    int   tilesX = 4;
    int   tilesZ = 4;

    float worldSize = 512.0f;    
    float heightScale = 300.0f;  

    int   gridRes = 129;

    int   maxLevel = 2;     
    float splitFactor = 4.0f;
    float mergeFactor = 6.0f;

    const char* heightmapPattern = "assets/volcano/Erosion2/Erosion2_Out_y%d_x%d.png";
    const char* albedoPattern = "assets/volcano/SatMap/SatMap_Out_y%d_x%d.png";
    const char* normalPattern = "assets/volcano/Normals/Normals_Out_y%d_x%d.png";

    void MakeHeightmapPath(int y, int x, char* out, size_t outSize) const;
    void MakeAlbedoPath(int y, int x, char* out, size_t outSize) const;
    void MakeNormalPath(int y, int x, char* out, size_t outSize) const;
};
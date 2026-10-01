#include "TerrainConfig.h"
#include <cstdio>

void TerrainConfig::MakeHeightmapPath(int y, int x, char* out, size_t outSize) const {
    sprintf_s(out, outSize, heightmapPattern, y, x);
}

void TerrainConfig::MakeAlbedoPath(int y, int x, char* out, size_t outSize) const {
    sprintf_s(out, outSize, albedoPattern, y, x);
}

void TerrainConfig::MakeNormalPath(int y, int x, char* out, size_t outSize) const {
    sprintf_s(out, outSize, normalPattern, y, x);
}
#include "TerrainLoader.h"
#include "../libs/stb_image.h"
#include <windows.h>
#include <cstdio>


std::vector<float> TerrainLoader::LoadHeightmap(const std::string& path,
    int& outWidth, int& outHeight)
{
    int width = 0, height = 0, channels = 0;


    unsigned short* data = stbi_load_16(path.c_str(), &width, &height,
        &channels, 1); 

    if (!data) {
        char buf[512];
        sprintf_s(buf, "[Terrain] Failed to load heightmap: %s (reason: %s)\n",
            path.c_str(), stbi_failure_reason());
        OutputDebugStringA(buf);
        outWidth = outHeight = 0;
        return {};
    }

    outWidth = width;
    outHeight = height;


    std::vector<float> result(width * height);
    const float inv = 1.0f / 65535.0f;
    for (int i = 0; i < width * height; ++i) {
        result[i] = data[i] * inv;
    }

    stbi_image_free(data);

    char buf[256];
    sprintf_s(buf, "[Terrain] Loaded heightmap: %s (%dx%d, %d channels)\n",
        path.c_str(), width, height, channels);
    OutputDebugStringA(buf);

    return result;
}

std::vector<uint8_t> TerrainLoader::LoadImage8(const std::string& path,
    int& outWidth, int& outHeight,
    int& outChannels)
{
    int width = 0, height = 0, channels = 0;

    unsigned char* data = stbi_load(path.c_str(), &width, &height,
        &channels, 0);  

    if (!data) {
        char buf[512];
        sprintf_s(buf, "[Terrain] Failed to load image: %s (reason: %s)\n",
            path.c_str(), stbi_failure_reason());
        OutputDebugStringA(buf);
        outWidth = outHeight = outChannels = 0;
        return {};
    }

    outWidth = width;
    outHeight = height;
    outChannels = channels;

    std::vector<uint8_t> result(data, data + width * height * channels);
    stbi_image_free(data);
    return result;
}
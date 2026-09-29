#pragma once
#include <string>
#include <vector>

class TerrainLoader {
public:
    static std::vector<float> LoadHeightmap(const std::string& path,
        int& outWidth, int& outHeight);

    static std::vector<uint8_t> LoadImage8(const std::string& path,
        int& outWidth, int& outHeight,
        int& outChannels);
};

#pragma once
#include <vector>
#include <string>
#include "Vertex.h"
#include "Material.h"

struct ModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials;
    std::vector<uint32_t> materialStartIndex;
    std::vector<uint32_t> materialIndexCount;
};

class ModelLoader {
public:
    static ModelData LoadOBJ(const std::string& filename, const std::string& basePath);

    static void DebugPrintInfo(const ModelData& model);
};
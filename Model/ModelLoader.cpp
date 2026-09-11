#include "ModelLoader.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <cstdio>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <windows.h>
#include <string>

using namespace DirectX;

// Функция генерации PBR параметров на основе имени материала (улучшенная эвристика)
static void GeneratePBRFromMaterialName(Material& material) {
    std::string name = material.name;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    // === МЕТАЛЛЫ (высокий metallic, низкий roughness) ===
    if (name.find("metal") != std::string::npos ||
        name.find("iron") != std::string::npos ||
        name.find("steel") != std::string::npos ||
        name.find("gold") != std::string::npos ||
        name.find("copper") != std::string::npos ||
        name.find("bronze") != std::string::npos ||
        name.find("brass") != std::string::npos ||
        name.find("flagpole") != std::string::npos ||      // флагшток Sponza
        name.find("chain") != std::string::npos) {         // цепи Sponza
        material.metallic = 0.8f;
        material.roughness = 0.3f;
        material.ao = 0.9f;
    }

    // === ДЕРЕВО ===
    else if (name.find("wood") != std::string::npos ||
        name.find("bark") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.8f;
        material.ao = 0.7f;
    }

    // === КАМЕНЬ (включая мрамор, гранит, кирпич) ===
    else if (name.find("stone") != std::string::npos ||
        name.find("rock") != std::string::npos ||
        name.find("brick") != std::string::npos ||
        name.find("marble") != std::string::npos ||
        name.find("granite") != std::string::npos ||
        name.find("column") != std::string::npos ||    // колонны Sponza
        name.find("pillar") != std::string::npos ||
        name.find("arch") != std::string::npos ||      // арки Sponza
        name.find("floor") != std::string::npos ||     // полы Sponza
        name.find("ceiling") != std::string::npos) {   // потолок Sponza
        material.metallic = 0.0f;
        material.roughness = 0.6f;  // слегка шероховатый
        material.ao = 0.9f;
    }

    // === КЕРАМИКА, ГЛИНА, ФАРФОР ===
    else if (name.find("vase") != std::string::npos ||      // вазы Sponza
        name.find("ceramic") != std::string::npos ||
        name.find("clay") != std::string::npos ||
        name.find("porcelain") != std::string::npos ||
        name.find("pottery") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.25f;  // гладкая поверхность
        material.ao = 0.9f;
    }

    // === ТКАНЬ, ТЕКСТИЛЬ ===
    else if (name.find("fabric") != std::string::npos ||
        name.find("cloth") != std::string::npos ||
        name.find("curtain") != std::string::npos ||
        name.find("carpet") != std::string::npos ||
        name.find("textile") != std::string::npos ||
        name.find("banner") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.9f;   // очень шероховатая
        material.ao = 0.6f;
    }

    // === ПЛАСТИК, ПОЛИМЕРЫ ===
    else if (name.find("plastic") != std::string::npos ||
        name.find("polymer") != std::string::npos ||
        name.find("resin") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.4f;
        material.ao = 1.0f;
    }

    // === СТЕКЛО ===
    else if (name.find("glass") != std::string::npos ||
        name.find("glazing") != std::string::npos ||
        name.find("window") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.05f;  // очень гладкое
        material.ao = 1.0f;
    }

    // === РЕЗИНА ===
    else if (name.find("rubber") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.9f;
        material.ao = 0.8f;
    }

    // === СТАТУИ, ДЕКОРАТИВНЫЕ ЭЛЕМЕНТЫ ===
    else if (name.find("lion") != std::string::npos ||      // статуи львов Sponza
        name.find("statue") != std::string::npos ||
        name.find("sculpture") != std::string::npos ||
        name.find("ornament") != std::string::npos ||
        name.find("detail") != std::string::npos ||    // детали Sponza
        name.find("thorn") != std::string::npos) {     // шипы Sponza
        material.metallic = 0.1f;   // слегка металлические
        material.roughness = 0.4f;
        material.ao = 0.9f;
    }

    // === ОБОИ, ФОН ===
    else if (name.find("background") != std::string::npos ||
        name.find("wallpaper") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.8f;
        material.ao = 1.0f;
    }

    // === КРЫША (часто грубая) ===
    else if (name.find("roof") != std::string::npos) {
        material.metallic = 0.0f;
        material.roughness = 0.8f;
        material.ao = 0.7f;
    }

    // === ПО УМОЛЧАНИЮ (если ничего не подошло) ===
    else {
        material.metallic = 0.0f;
        material.roughness = 0.5f;   // средняя шероховатость
        material.ao = 1.0f;
    }
}

ModelData ModelLoader::LoadOBJ(const std::string& filename, const std::string& basePath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
        filename.c_str(), basePath.c_str());

    if (!warn.empty()) OutputDebugStringA(("OBJ Warning: " + warn + "\n").c_str());
    if (!err.empty()) OutputDebugStringA(("OBJ Error: " + err + "\n").c_str());

    if (!ret || shapes.empty()) {
        throw std::runtime_error("Failed to load OBJ: " + filename);
    }

    ModelData model;
    const float scale = 0.01f;

    // 1. Загружаем материалы
    std::map<int, int> materialIdToIndex;
    for (size_t i = 0; i < materials.size(); i++) {
        const auto& mat = materials[i];
        Material material;
        material.name = mat.name;
        material.ambient = XMFLOAT3(mat.ambient[0], mat.ambient[1], mat.ambient[2]);
        material.diffuse = XMFLOAT3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
        material.specular = XMFLOAT3(mat.specular[0], mat.specular[1], mat.specular[2]);
        material.shininess = mat.shininess;

        if (!mat.diffuse_texname.empty()) {
            material.diffuseTexturePath = basePath + "/" + mat.diffuse_texname;
        }

        // Генерируем PBR параметры на основе имени материала
        GeneratePBRFromMaterialName(material);

        material.textureIndex = -1;
        model.materials.push_back(material);
        materialIdToIndex[(int)i] = (int)i;
    }

    // Если нет материалов, добавляем дефолтный
    if (model.materials.empty()) {
        Material defaultMat;
        defaultMat.name = "default";
        defaultMat.ambient = XMFLOAT3(0.3f, 0.25f, 0.2f);
        defaultMat.diffuse = XMFLOAT3(0.9f, 0.8f, 0.7f);
        defaultMat.specular = XMFLOAT3(0.2f, 0.2f, 0.2f);
        defaultMat.shininess = 16.0f;
        defaultMat.metallic = 0.0f;
        defaultMat.roughness = 0.5f;
        defaultMat.ao = 1.0f;
        model.materials.push_back(defaultMat);
        materialIdToIndex[-1] = 0;
    }

    // 2. Собираем все вершины и индексы, запоминая материал для каждого полигона
    struct Face {
        uint32_t v0, v1, v2;
        int materialId;
    };
    std::vector<Face> faces;
    std::vector<Vertex> allVertices;

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            if (fv != 3) {
                indexOffset += fv;
                continue;
            }

            int materialId = shape.mesh.material_ids[f];
            int materialIndex = 0;
            if (materialId >= 0 && materialId < (int)materials.size()) {
                materialIndex = materialIdToIndex[materialId];
            }

            uint32_t baseIndex = (uint32_t)allVertices.size();
            uint32_t indices[3];

            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                float px = attrib.vertices[3 * idx.vertex_index + 0] * scale;
                float py = attrib.vertices[3 * idx.vertex_index + 1] * scale;
                float pz = attrib.vertices[3 * idx.vertex_index + 2] * scale;

                float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                if (idx.normal_index >= 0) {
                    nx = attrib.normals[3 * idx.normal_index + 0];
                    ny = attrib.normals[3 * idx.normal_index + 1];
                    nz = attrib.normals[3 * idx.normal_index + 2];
                }

                float tx = 0.0f, ty = 0.0f;
                if (idx.texcoord_index >= 0) {
                    tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                    ty = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                allVertices.push_back({ XMFLOAT3(px, py, pz),
                                        XMFLOAT3(nx, ny, nz),
                                        XMFLOAT2(tx, ty) });
                indices[v] = (uint32_t)(allVertices.size() - 1);
            }

            faces.push_back({ indices[0], indices[1], indices[2], materialIndex });
            indexOffset += fv;
        }
    }

    // 3. Оптимизация: сортируем грани по материалу для лучшей производительности
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
        return a.materialId < b.materialId;
        });

    // 4. Формируем финальные вершины и индексы
    model.vertices.clear();
    model.indices.clear();
    model.materialStartIndex.clear();
    model.materialIndexCount.clear();

    int currentMaterial = -1;
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;

    for (const auto& face : faces) {
        if (face.materialId != currentMaterial) {
            if (currentMaterial != -1) {
                model.materialStartIndex.push_back(startIndex);
                model.materialIndexCount.push_back(indexCount);
            }
            currentMaterial = face.materialId;
            startIndex = (uint32_t)model.indices.size();
            indexCount = 0;
        }

        // Добавляем вершины (с дубликатами для независимости материалов)
        uint32_t baseIdx = (uint32_t)model.vertices.size();
        model.vertices.push_back(allVertices[face.v0]);
        model.vertices.push_back(allVertices[face.v1]);
        model.vertices.push_back(allVertices[face.v2]);

        model.indices.push_back(baseIdx);
        model.indices.push_back(baseIdx + 1);
        model.indices.push_back(baseIdx + 2);

        indexCount += 3;
    }

    if (indexCount > 0) {
        model.materialStartIndex.push_back(startIndex);
        model.materialIndexCount.push_back(indexCount);
    }

    // 5. Диагностика
    char buffer[512];
    sprintf_s(buffer, "\n=== Model Loaded ===\n");
    OutputDebugStringA(buffer);
    sprintf_s(buffer, "Vertices: %zu\n", model.vertices.size());
    OutputDebugStringA(buffer);
    sprintf_s(buffer, "Indices: %zu\n", model.indices.size());
    OutputDebugStringA(buffer);
    sprintf_s(buffer, "Materials: %zu\n", model.materials.size());
    OutputDebugStringA(buffer);
    sprintf_s(buffer, "Material Groups: %zu\n", model.materialStartIndex.size());
    OutputDebugStringA(buffer);

    for (size_t i = 0; i < model.materialStartIndex.size() && i < model.materials.size(); i++) {
        sprintf_s(buffer, "Group[%zu]: material='%s', metallic=%.2f, roughness=%.2f, ao=%.2f, startIndex=%u, count=%u\n",
            i, model.materials[i].name.c_str(),
            model.materials[i].metallic,
            model.materials[i].roughness,
            model.materials[i].ao,
            model.materialStartIndex[i], model.materialIndexCount[i]);
        OutputDebugStringA(buffer);
    }

    return model;
}

void ModelLoader::DebugPrintInfo(const ModelData& model) {
    char buffer[256];
    sprintf_s(buffer, "Model: %zu vertices, %zu indices in %zu groups\n",
        model.vertices.size(), model.indices.size(), model.materialStartIndex.size());
    OutputDebugStringA(buffer);
}
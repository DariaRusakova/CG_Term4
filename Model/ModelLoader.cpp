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

    // Конвертируем материалы tinyobj в наши материалы
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
        model.materials.push_back(defaultMat);
        materialIdToIndex[-1] = 0;
    }

    // Группировка по материалам
    std::vector<std::vector<uint32_t>> materialGroups(model.materials.size());
    std::vector<bool> materialUsed(model.materials.size(), false);

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
            else {
                materialIndex = 0;
            }

            materialUsed[materialIndex] = true;

            uint32_t baseIndex = (uint32_t)model.vertices.size();

            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                // Позиция
                float px = attrib.vertices[3 * idx.vertex_index + 0] * scale;
                float py = attrib.vertices[3 * idx.vertex_index + 1] * scale;
                float pz = attrib.vertices[3 * idx.vertex_index + 2] * scale;

                // Нормаль
                float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                if (idx.normal_index >= 0) {
                    nx = attrib.normals[3 * idx.normal_index + 0];
                    ny = attrib.normals[3 * idx.normal_index + 1];
                    nz = attrib.normals[3 * idx.normal_index + 2];
                }

                // Текстурные координаты
                float tx = 0.0f, ty = 0.0f;
                if (idx.texcoord_index >= 0) {
                    tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                    ty = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                model.vertices.push_back({ XMFLOAT3(px, py, pz),
                                           XMFLOAT3(nx, ny, nz),
                                           XMFLOAT2(tx, ty) });
                model.indices.push_back((uint32_t)(model.indices.size()));
            }

            for (int v = 0; v < 3; v++) {
                materialGroups[materialIndex].push_back(baseIndex + v);
            }

            indexOffset += fv;
        }
    }

    // Формируем финальные массивы
    for (size_t i = 0; i < model.materials.size(); i++) {
        if (materialUsed[i] && !materialGroups[i].empty()) {
            model.materialStartIndex.push_back(materialGroups[i][0]);
            model.materialIndexCount.push_back((uint32_t)materialGroups[i].size());
        }
        else if (!materialGroups[i].empty()) {
            model.materialStartIndex.push_back(materialGroups[i][0]);
            model.materialIndexCount.push_back((uint32_t)materialGroups[i].size());
        }
    }

    // Выводим отладочную информацию
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

    return model;
}

void ModelLoader::DebugPrintInfo(const ModelData& model) {
    char buffer[256];
    sprintf_s(buffer, "Model: %zu vertices, %zu indices in %zu groups\n",
        model.vertices.size(), model.indices.size(), model.materialStartIndex.size());
    OutputDebugStringA(buffer);

    for (size_t i = 0; i < model.materialStartIndex.size() && i < model.materials.size(); i++) {
        sprintf_s(buffer, "  Group %zu: Material '%s', Start=%u, Count=%u\n",
            i, model.materials[i].name.c_str(),
            model.materialStartIndex[i], model.materialIndexCount[i]);
        OutputDebugStringA(buffer);
    }
}
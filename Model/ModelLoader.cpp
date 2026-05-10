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

// Вспомогательная функция для вычисления касательных
static void CalculateTangents(ModelData& model) {
    if (model.vertices.empty() || model.indices.empty()) return;

    // Инициализируем касательные и бикасательные нулями
    for (auto& vertex : model.vertices) {
        vertex.tangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
        vertex.bitangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
    }

    // Обрабатываем каждый треугольник
    for (size_t i = 0; i < model.indices.size() - 2; i += 3) {
        uint32_t i0 = model.indices[i];
        uint32_t i1 = model.indices[i + 1];
        uint32_t i2 = model.indices[i + 2];

        // Проверка на валидность индексов
        if (i0 >= model.vertices.size() || i1 >= model.vertices.size() || i2 >= model.vertices.size())
            continue;

        Vertex& v0 = model.vertices[i0];
        Vertex& v1 = model.vertices[i1];
        Vertex& v2 = model.vertices[i2];

        // Вычисляем рёбра треугольника
        XMFLOAT3 edge1 = {
            v1.position.x - v0.position.x,
            v1.position.y - v0.position.y,
            v1.position.z - v0.position.z
        };
        XMFLOAT3 edge2 = {
            v2.position.x - v0.position.x,
            v2.position.y - v0.position.y,
            v2.position.z - v0.position.z
        };

        // Вычисляем разницу текстурных координат
        float deltaU1 = v1.texcoord.x - v0.texcoord.x;
        float deltaV1 = v1.texcoord.y - v0.texcoord.y;
        float deltaU2 = v2.texcoord.x - v0.texcoord.x;
        float deltaV2 = v2.texcoord.y - v0.texcoord.y;

        // Вычисляем детерминант
        float f = 1.0f / (deltaU1 * deltaV2 - deltaU2 * deltaV1);

        // Вычисляем касательную
        XMFLOAT3 tangent;
        tangent.x = f * (deltaV2 * edge1.x - deltaV1 * edge2.x);
        tangent.y = f * (deltaV2 * edge1.y - deltaV1 * edge2.y);
        tangent.z = f * (deltaV2 * edge1.z - deltaV1 * edge2.z);

        // Аккумулируем касательные для вершин
        v0.tangent.x += tangent.x;
        v0.tangent.y += tangent.y;
        v0.tangent.z += tangent.z;

        v1.tangent.x += tangent.x;
        v1.tangent.y += tangent.y;
        v1.tangent.z += tangent.z;

        v2.tangent.x += tangent.x;
        v2.tangent.y += tangent.y;
        v2.tangent.z += tangent.z;
    }

    // Ортогонализируем и нормализуем касательные
    for (auto& vertex : model.vertices) {
        // Загружаем векторы в XMVECTOR для математических операций
        XMVECTOR T = XMLoadFloat3(&vertex.tangent);
        XMVECTOR N = XMLoadFloat3(&vertex.normal);

        // Ортогонализация Грама-Шмидта
        XMVECTOR T_perp = XMVector3Normalize(
            XMVectorSubtract(T, XMVectorMultiply(N, XMVector3Dot(N, T)))
        );

        // Сохраняем ортогонализированную касательную
        XMStoreFloat3(&vertex.tangent, T_perp);

        // Вычисляем бикасательную как cross product
        XMVECTOR B = XMVector3Cross(N, T_perp);

        // Проверяем направление бикасательной
        XMVECTOR B_check = XMVector3Cross(N, T_perp);
        if (XMVectorGetX(XMVector3Dot(B_check, B)) < 0.0f) {
            B = XMVectorNegate(B);
        }

        XMStoreFloat3(&vertex.bitangent, B);
    }

    OutputDebugStringA("Tangent space calculated successfully\n");
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
    const float scale = 1.0f;    // Если модель изначально маленькая
    // const float scale = 10.0f;  // Если нужно увеличить
    // const float scale = 0.01f;  // Для Sponza (уменьшение)

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
                                           XMFLOAT2(tx, ty),
                                           XMFLOAT3(0, 0, 0),  // tangent (будет вычислен)
                                           XMFLOAT3(0, 0, 0) }); // bitangent (будет вычислен)
            }

            // Добавляем индексы
            for (int v = 0; v < 3; v++) {
                model.indices.push_back(baseIndex + v);
                materialGroups[materialIndex].push_back(baseIndex + v);
            }

            indexOffset += fv;
        }
    }

    // ВАЖНО: Вычисляем tangent space ДО формирования групп материалов
    CalculateTangents(model);

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
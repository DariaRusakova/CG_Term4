#pragma once
#include <string>
#include <DirectXMath.h>

struct Material {
    std::string name;
    DirectX::XMFLOAT3 ambient = DirectX::XMFLOAT3(0.2f, 0.2f, 0.2f);
    DirectX::XMFLOAT3 diffuse = DirectX::XMFLOAT3(0.8f, 0.8f, 0.8f);
    DirectX::XMFLOAT3 specular = DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f);
    float shininess = 32.0f;
    std::string diffuseTexturePath;
    int textureIndex = -1;

    // PBR параметры
    float metallic = 0.0f;      // 0 - диэлектрик, 1 - металл
    float roughness = 0.5f;     // 0 - гладкий, 1 - шероховатый
    float ao = 1.0f;            // Ambient occlusion (1.0 = нет затенения)

    // PBR текстуры (опционально)
    std::string metallicRoughnessTexturePath;
    std::string aoTexturePath;
    std::string normalTexturePath;

    int metallicRoughnessTextureIndex = -1;
    int aoTextureIndex = -1;
    int normalTextureIndex = -1;
};
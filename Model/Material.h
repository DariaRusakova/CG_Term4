#pragma once
#include <string>
#include <DirectXMath.h>

struct Material {
    std::string name;

    // PBR-параметры (используются, если нет текстуры)
    DirectX::XMFLOAT3 albedoFactor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
    float             roughnessFactor = 0.5f;
    float             metallicFactor = 0.0f;
    float             aoFactor = 1.0f;

    // Пути к текстурам
    std::string albedoTexturePath;
    std::string normalTexturePath;
    std::string roughnessTexturePath;
    std::string metallicTexturePath;
    std::string aoTexturePath;

    // Индексы в общем SRV heap (глобальные, по всем объектам)
    int albedoSrv = -1;
    int normalSrv = -1;
    int roughnessSrv = -1;
    int metallicSrv = -1;
    int aoSrv = -1;

    std::string diffuseTexturePath;
    int textureIndex = -1;
};
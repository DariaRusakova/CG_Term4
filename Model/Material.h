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
};
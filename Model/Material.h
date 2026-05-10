#pragma once
#include <string>
#include <DirectXMath.h>

struct Material {
    std::string name;
    DirectX::XMFLOAT3 ambient = { 0.3f, 0.25f, 0.2f };
    DirectX::XMFLOAT3 diffuse = { 0.9f, 0.8f, 0.7f };
    DirectX::XMFLOAT3 specular = { 0.2f, 0.2f, 0.2f };
    float shininess = 16.0f;
    std::string diffuseTexturePath;
    int textureIndex = -1;
};
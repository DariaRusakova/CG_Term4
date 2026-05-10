// Rendering/Light.h
#pragma once
#include <DirectXMath.h>

enum class LightType {
    Point = 0,
    Directional = 1,
    Spot = 2
};

struct Light {
    LightType type;
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 direction;
    DirectX::XMFLOAT4 color;
    float intensity;
    float range;
    float spotAngle;
    float padding;

    DirectX::XMFLOAT4 GetAsFloat4() const {
        return DirectX::XMFLOAT4(position.x, position.y, position.z, (float)type);
    }

    DirectX::XMFLOAT4 GetDirectionAsFloat4() const {
        return DirectX::XMFLOAT4(direction.x, direction.y, direction.z, 0.0f);
    }
};
#pragma once
#include <DirectXMath.h>
#include <windows.h>  // Для UINT и других типов Windows

enum class LightType {
    Point = 0,
    Directional = 1,
    Spot = 2
};

struct Light {
    LightType type = LightType::Point;
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;
    float spotAngle = DirectX::XM_PIDIV4;

    DirectX::XMFLOAT4 GetAsFloat4() const {
        return DirectX::XMFLOAT4(position.x, position.y, position.z, static_cast<float>(type));
    }

    DirectX::XMFLOAT4 GetDirectionAsFloat4() const {
        return DirectX::XMFLOAT4(direction.x, direction.y, direction.z, 0.0f);
    }
};

// Структура для передачи света в константный буфер
struct LightBuffer {
    DirectX::XMFLOAT4 position_type[16];      // xyz=position, w=type
    DirectX::XMFLOAT4 direction[16];          // xyz=direction, w=unused
    DirectX::XMFLOAT4 color_intensity[16];    // rgb=color, a=intensity
    DirectX::XMFLOAT4 range_spotAngle[16];    // x=range, y=spotAngle, zw=unused
    UINT lightCount;
    DirectX::XMFLOAT3 padding;                // Выравнивание до 16 байт
};
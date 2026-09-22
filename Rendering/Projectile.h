#pragma once
#include <directxmath.h>

struct Projectile {
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    DirectX::XMFLOAT4 color = { 1, 1, 1, 1 };
    float radius = 0.5f;
    float intensity = 25.0f;
    float range = 10.0f;
    float lifetime = 3.0f;
    float age = 0.0f;
    bool  alive = false;
};
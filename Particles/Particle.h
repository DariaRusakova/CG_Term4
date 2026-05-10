#pragma once
#include <DirectXMath.h>

// Структура частицы (должна быть выровнена для StructuredBuffer)
struct Particle {
    DirectX::XMFLOAT3 position;      // Позиция в world space
    float age;                       // Текущий возраст (0 = новая, >1 = мёртвая)

    DirectX::XMFLOAT3 velocity;      // Скорость
    float lifetime;                  // Максимальное время жизни

    DirectX::XMFLOAT4 color;         // Цвет (RGBA)
    float size;                      // Размер билборда
    float padding[3];                // Выравнивание до 64 байт (кратно 16)

    Particle()
        : position(0, 0, 0), age(0)
        , velocity(0, 0, 0), lifetime(1.0f)
        , color(1, 1, 1, 1), size(1.0f) {
        padding[0] = padding[1] = padding[2] = 0;
    }
};
static_assert(sizeof(Particle) % 16 == 0, "Particle must be 16-byte aligned");
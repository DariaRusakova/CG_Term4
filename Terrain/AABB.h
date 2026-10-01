#pragma once
#include <DirectXMath.h>
#include <algorithm>

struct AABB {
    DirectX::XMFLOAT3 min = { 1e9f,  1e9f,  1e9f };
    DirectX::XMFLOAT3 max = { -1e9f, -1e9f, -1e9f };

    void Expand(const DirectX::XMFLOAT3& p) {
        min.x = (std::min)(min.x, p.x);
        min.y = (std::min)(min.y, p.y);
        min.z = (std::min)(min.z, p.z);
        max.x = (std::max)(max.x, p.x);
        max.y = (std::max)(max.y, p.y);
        max.z = (std::max)(max.z, p.z);
    }

    DirectX::XMFLOAT3 GetCenter() const {
        return { (min.x + max.x) * 0.5f,
                 (min.y + max.y) * 0.5f,
                 (min.z + max.z) * 0.5f };
    }

    DirectX::XMFLOAT3 GetExtents() const {
        return { (max.x - min.x) * 0.5f,
                 (max.y - min.y) * 0.5f,
                 (max.z - min.z) * 0.5f };
    }

    bool IsValid() const { return min.x <= max.x; }
};
#pragma once
#include <DirectXMath.h>
#include "AABB.h"

class Frustum {
public:
    void ExtractFromViewProj(const DirectX::XMFLOAT4X4& viewProj);
    bool Intersects(const AABB& box) const;

private:
    DirectX::XMFLOAT4 m_planes[6];
};
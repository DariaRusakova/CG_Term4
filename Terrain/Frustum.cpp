#include "Frustum.h"

using namespace DirectX;

void Frustum::ExtractFromViewProj(const XMFLOAT4X4& m) {
    XMFLOAT4 row0 = { m._11, m._21, m._31, m._41 };
    XMFLOAT4 row1 = { m._12, m._22, m._32, m._42 };
    XMFLOAT4 row2 = { m._13, m._23, m._33, m._43 };
    XMFLOAT4 row3 = { m._14, m._24, m._34, m._44 };

    auto sub = [](XMFLOAT4 a, XMFLOAT4 b) {
        return XMFLOAT4{ a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
        };
    auto add = [](XMFLOAT4 a, XMFLOAT4 b) {
        return XMFLOAT4{ a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
        };


    m_planes[0] = add(row3, row0);   // left
    m_planes[1] = sub(row3, row0);   // right
    m_planes[2] = add(row3, row1);   // bottom
    m_planes[3] = sub(row3, row1);   // top
    m_planes[4] = row2;              // near
    m_planes[5] = sub(row3, row2);   // far

    for (auto& p : m_planes) {
        float len = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len > 1e-6f) {
            p.x /= len; p.y /= len; p.z /= len; p.w /= len;
        }
    }
}

bool Frustum::Intersects(const AABB& box) const {
    if (!box.IsValid()) return true; 


    for (int i = 0; i < 6; ++i) {
        const XMFLOAT4& p = m_planes[i];

        float px = (p.x >= 0.0f) ? box.max.x : box.min.x;
        float py = (p.y >= 0.0f) ? box.max.y : box.min.y;
        float pz = (p.z >= 0.0f) ? box.max.z : box.min.z;

        float dist = p.x * px + p.y * py + p.z * pz + p.w;
        if (dist < 0.0f) {
            return false;  
        }
    }
    return true;
}
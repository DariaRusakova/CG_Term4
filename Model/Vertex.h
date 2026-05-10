#pragma once
#include <DirectXMath.h>

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texcoord;
    DirectX::XMFLOAT3 tangent;
    DirectX::XMFLOAT3 bitangent;

    Vertex()
        : position(0, 0, 0)
        , normal(0, 1, 0)
        , texcoord(0, 0)
        , tangent(1, 0, 0)
        , bitangent(0, 1, 0) {
    }

    Vertex(DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 norm, DirectX::XMFLOAT2 tex,
        DirectX::XMFLOAT3 tan = { 1,0,0 }, DirectX::XMFLOAT3 bitan = { 0,1,0 })
        : position(pos), normal(norm), texcoord(tex), tangent(tan), bitangent(bitan) {
    }
};
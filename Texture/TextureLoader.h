#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>

struct Texture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;
    int width = 0;
    int height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::string filename;
};

class TextureLoader {
public:
    static Texture LoadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::string& filename);
    static Texture CreateDefaultTexture(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
};
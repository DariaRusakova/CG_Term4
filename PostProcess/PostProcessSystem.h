#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <memory>

class PostProcessSystem
{
public:
    enum class EffectType
    {
        ReadGBuffer = 0,
        Sepia = 1,
        EdgeDetection = 2,
        Off = 3,        
        Count = 4      
    };

    PostProcessSystem();
    ~PostProcessSystem();

    void Initialize(ID3D12Device* device, UINT width, UINT height);
    void Release();
    void Resize(UINT width, UINT height);

    void SetGBufferResources(
        ID3D12Resource* albedoTexture,
        ID3D12Resource* worldPosTexture,
        ID3D12Resource* normalTexture
    );

    void Render(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE targetRTV,
        UINT targetWidth,
        UINT targetHeight,
        EffectType effectType = EffectType::Sepia
    );

private:
    void CreateRootSignature(ID3D12Device* device);
    void CreatePipelineStates(ID3D12Device* device);
    void CreateDescriptorHeaps(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStates[static_cast<int>(EffectType::Count)];

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_gbufferSrvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_gbufferSRVs[3];
    D3D12_GPU_DESCRIPTOR_HANDLE m_gbufferSRVGPU[3];

    ID3D12Resource* m_albedoTexture = nullptr;
    ID3D12Resource* m_worldPosTexture = nullptr;
    ID3D12Resource* m_normalTexture = nullptr;

    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_srvDescriptorSize = 0;
    UINT m_rtvDescriptorSize = 0;

    bool m_initialized = false;
};
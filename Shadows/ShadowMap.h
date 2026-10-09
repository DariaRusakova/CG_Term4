#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

struct alignas(256) ShadowMapCB {
    DirectX::XMFLOAT4X4 lightViewProj[4];   
    DirectX::XMFLOAT4   cascadeSplits;     
    DirectX::XMFLOAT4   lightDirection;
    DirectX::XMFLOAT4   shadowBias;
    DirectX::XMFLOAT4   textureSize;       
    DirectX::XMFLOAT4   cameraPos;
    DirectX::XMFLOAT4   lightPos;
    DirectX::XMFLOAT4   cameraForward;
    int                 cascadeIndex;       
    int                 _pad[3];
};
static_assert(sizeof(ShadowMapCB) % 256 == 0, "CB must be 256-aligned");

class ShadowMapSystem {
public:
    static const int CASCADE_COUNT = 4;
    static const int SHADOW_MAP_SIZE = 2048;

    void Initialize(ID3D12Device* device);

    void UpdateCascades(const DirectX::XMFLOAT3& lightDir,
        const DirectX::XMFLOAT3& cameraPos,
        const DirectX::XMFLOAT3& cameraForward,
        float fovY,
        float aspect,
        float nearZ,
        float farZ);

    ID3D12Resource* GetDepthResource() const { return m_shadowMap.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVForCascade(int cascade) const {
        return m_cascadeDSV[cascade];
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPU() const {
        return m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPU() const {
        return m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    }
    ID3D12DescriptorHeap* GetSRVHeap() const { return m_srvHeap.Get(); }

    const ShadowMapCB& GetConstantBufferData() const { return m_cbData; }
    ID3D12Resource* GetConstantBuffer(int cascade) const { return m_cbResource[cascade].Get(); }


    void SetCascadeIndex(int idx) {
        m_cbData.cascadeIndex = idx;
        memcpy(m_cbDataBegin, &m_cbData, sizeof(ShadowMapCB));
    }
    void SetCurrentCascadeIndex(int index);
private:
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_shadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;

    D3D12_CPU_DESCRIPTOR_HANDLE m_cascadeDSV[CASCADE_COUNT];

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbResource[CASCADE_COUNT];
    void* m_cbDataBegin[CASCADE_COUNT] = {};
    ShadowMapCB  m_cbData{};

    void CreateDescriptorHeaps(ID3D12Device* device);
    void CreateTexture(ID3D12Device* device);
    void CreateConstantBuffer(ID3D12Device* device);

};

namespace ShadowShaders {
    static const char* VS = R"(
        cbuffer ShadowCB : register(b0) {
            float4x4 lightViewProj[4];
            float4   cascadeSplits;
            float4   lightDirection;
            float4   shadowBias;
            float4   textureSize;
            float4   cameraPos;
            float4   lightPos;
            float4   cameraForward;
            int      cascadeIndex;
            int3     _pad;
        };

        // b1 = SceneConstantBuffer, но имена переименованы, чтобы не конфликтовать с ShadowCB
        cbuffer ObjectCB : register(b1) {
            float4x4 obj_worldViewProj;
            float4x4 obj_world;
            float4   obj_lightPos;
            float4   obj_lightColor;
            float4   obj_cameraPos;
            float2   obj_textureScale;
            float2   obj_textureOffset;
            float4   obj_materialFactor;
            float4   obj_padding[2];
        };

        struct VSInput {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 texcoord : TEXCOORD;
        };

        float4 main(VSInput input) : SV_POSITION {
            float4 localPos = float4(input.position, 1.0);
            float4 worldPos = mul(localPos, obj_world);
            return mul(worldPos, lightViewProj[cascadeIndex]);
        }
    )";

    static const char* PS = R"(
        void main() { }
    )";
}
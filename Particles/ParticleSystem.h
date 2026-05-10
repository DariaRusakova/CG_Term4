#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include "Particle.h"

class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    void Initialize(ID3D12Device* device, UINT maxParticles);
    void Update(float deltaTime, const DirectX::XMFLOAT3& emitterPosition);
    void Render(ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

    void SetEmitRate(float rate) { m_emitRate = rate; }
    void SetParticleLifetime(float lt) { m_particleLifetime = lt; }
    void SetParticleColor(const DirectX::XMFLOAT4& c) { m_particleColor = c; }
    void SetParticleSize(float s) { m_particleSize = s; }
    void SetParticleSpeed(float s) { m_particleSpeed = s; }

    void SetViewProjection(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool e) { m_enabled = e; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;

    // Буферы для частиц (простой StructuredBuffer, без Append/Consume для простоты)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;

    // Состояния
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_renderPSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_renderRootSig;

    UINT m_maxParticles = 0;
    bool m_enabled = false;
    void* m_mappedCB = nullptr;
    void* m_mappedParticles = nullptr;

    // Параметры
    float m_emitRate = 100.0f;
    float m_particleLifetime = 2.0f;
    DirectX::XMFLOAT4 m_particleColor = { 1, 0.8f, 0.3f, 1 };
    float m_particleSize = 0.5f;
    float m_particleSpeed = 3.0f;
    float m_timeAccumulator = 0;
    UINT m_frameIndex = 0;

    DirectX::XMMATRIX m_viewMatrix;
    DirectX::XMMATRIX m_projMatrix;

    struct ParticleCB {
        DirectX::XMFLOAT4X4 viewProjMatrix;
        DirectX::XMFLOAT2 screenSize;
        float particleSize;
        float padding;
    };

    struct CPUParticle {
        DirectX::XMFLOAT3 position;
        float age;
        DirectX::XMFLOAT3 velocity;
        float lifetime;
        DirectX::XMFLOAT4 color;
        float size;
        float padding[3];
    };

    std::vector<CPUParticle> m_cpuParticles;

    void CreateBuffers();
    void CreatePipeline();
    void EmitParticles(float deltaTime, const DirectX::XMFLOAT3& emitterPos);
    void UpdateParticlesCPU(float deltaTime);
};
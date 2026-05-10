#include "ParticleSystem.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <windows.h>
#include <ctime>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
    inline void ThrowIfFailed(HRESULT hr, const char* msg) {
        if (FAILED(hr)) {
            char buf[512];
            sprintf_s(buf, "ParticleSystem: %s (0x%08X)\n", msg, hr);
            OutputDebugStringA(buf);
            throw std::runtime_error(buf);
        }
    }

    float RandomFloat() {
        return (float)rand() / (float)RAND_MAX;
    }
}

ParticleSystem::ParticleSystem() {
    srand((UINT)time(nullptr));
}

ParticleSystem::~ParticleSystem() = default;

void ParticleSystem::Initialize(ID3D12Device* device, UINT maxParticles) {
    m_device = device;
    m_maxParticles = maxParticles;
    m_cpuParticles.reserve(maxParticles);

    CreateBuffers();
    CreatePipeline();

    m_enabled = true;
    OutputDebugStringA("ParticleSystem initialized (simplified CPU update)\n");
}

void ParticleSystem::CreateBuffers() {
    // Буфер для частиц (upload heap для простоты)
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(CPUParticle) * m_maxParticles;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_particleUploadBuffer)), "Create particle upload buffer");

    D3D12_RANGE readRange = { 0, 0 };
    m_particleUploadBuffer->Map(0, &readRange, &m_mappedParticles);

    // Константный буфер
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = 256;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_constantBuffer)), "Create constant buffer");

    m_constantBuffer->Map(0, &readRange, &m_mappedCB);
}

void ParticleSystem::CreatePipeline() {
    // Root Signature для билбордов
    D3D12_ROOT_PARAMETER rootParams[2];

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
    sigDesc.NumParameters = 2;
    sigDesc.pParameters = rootParams;
    sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature, error;
    HRESULT hr = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Serialize root sig");
    }

    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_renderRootSig));
    ThrowIfFailed(hr, "Create root sig");
    OutputDebugStringA("Particle root signature OK\n");

    // Шейдеры
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, gs, ps;

    static const char* vsCode = R"(
        struct Particle {
            float3 position;
            float age;
            float3 velocity;
            float lifetime;
            float4 color;
            float size;
            float3 padding;
        };
        
        struct GSInput {
            float3 position : POSITION;
            float age       : TEXCOORD0;
            float4 color    : COLOR;
            float size      : TEXCOORD1;
        };
        
        StructuredBuffer<Particle> particles : register(t0);
        
        GSInput main(uint vertexID : SV_VertexID) {
            Particle p = particles[vertexID];
            
            GSInput output;
            output.position = p.position;
            output.age = p.age;
            output.color = p.color;
            output.size = p.size + p.age * 0.5f;
            return output;
        }
    )";

    hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", flags, 0, &vs, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Compile VS");
    }
    OutputDebugStringA("Particle VS OK\n");

    static const char* gsCode = R"(
        struct GSInput {
            float3 position : POSITION;
            float age       : TEXCOORD0;
            float4 color    : COLOR;
            float size      : TEXCOORD1;
        };
        
        struct PSInput {
            float4 position : SV_POSITION;
            float2 texcoord : TEXCOORD0;
            float4 color    : COLOR;
            float age       : TEXCOORD1;
        };
        
        cbuffer ParticleCB : register(b0) {
            float4x4 viewProjMatrix;
            float2 screenSize;
            float particleSize;
            float padding;
        }
        
        [maxvertexcount(4)]
        void main(point GSInput input[1], inout TriangleStream<PSInput> triStream) {
            PSInput output;
            
            float3 pos = input[0].position;
            float halfSize = input[0].size * 0.5f;
            float alpha = saturate(1.0f - input[0].age);
            output.color = float4(input[0].color.rgb, input[0].color.a * alpha);
            output.age = input[0].age;
            
            float4 p0 = mul(float4(pos + float3(-halfSize, -halfSize, 0), 1.0), viewProjMatrix);
            float4 p1 = mul(float4(pos + float3( halfSize, -halfSize, 0), 1.0), viewProjMatrix);
            float4 p2 = mul(float4(pos + float3(-halfSize,  halfSize, 0), 1.0), viewProjMatrix);
            float4 p3 = mul(float4(pos + float3( halfSize,  halfSize, 0), 1.0), viewProjMatrix);
            
            output.position = p0; output.texcoord = float2(0, 0); triStream.Append(output);
            output.position = p1; output.texcoord = float2(1, 0); triStream.Append(output);
            output.position = p2; output.texcoord = float2(0, 1); triStream.Append(output);
            output.position = p3; output.texcoord = float2(1, 1); triStream.Append(output);
            
            triStream.RestartStrip();
        }
    )";

    hr = D3DCompile(gsCode, strlen(gsCode), nullptr, nullptr, nullptr, "main", "gs_5_0", flags, 0, &gs, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Compile GS");
    }
    OutputDebugStringA("Particle GS OK\n");

    static const char* psCode = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float2 texcoord : TEXCOORD0;
            float4 color    : COLOR;
            float age       : TEXCOORD1;
        };
        
        float4 main(PSInput input) : SV_TARGET {
            float2 center = input.texcoord - 0.5f;
            float dist = length(center);
            float alpha = 1.0f - smoothstep(0.4f, 0.5f, dist);
            alpha *= input.color.a;
            if (alpha < 0.01f) discard;
            return float4(input.color.rgb, alpha);
        }
    )";

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", flags, 0, &ps, &error);
    if (FAILED(hr)) {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailed(hr, "Compile PS");
    }
    OutputDebugStringA("Particle PS OK\n");

    // PSO - БЕЗ input layout, используем пустой
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 };  // <-- ВАЖНО: пустой input layout!
    psoDesc.pRootSignature = m_renderRootSig.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.GS = { gs->GetBufferPointer(), gs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 0;
    psoDesc.RasterizerState.DepthBiasClamp = 0;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 0;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.NodeMask = 1;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_renderPSO));
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "Create PSO failed: 0x%08X\n", hr);
        OutputDebugStringA(buf);
    }
    ThrowIfFailed(hr, "Create PSO");
    OutputDebugStringA("Particle PSO created OK\n");
}

void ParticleSystem::SetViewProjection(const XMMATRIX& view, const XMMATRIX& proj) {
    m_viewMatrix = view;
    m_projMatrix = proj;
}

void ParticleSystem::EmitParticles(float deltaTime, const XMFLOAT3& emitterPos) {
    UINT toEmit = (UINT)(m_emitRate * deltaTime);
    if (toEmit > 100) toEmit = 100; // Ограничение за кадр

    for (UINT i = 0; i < toEmit; i++) {
        if (m_cpuParticles.size() >= m_maxParticles) break;

        CPUParticle p;
        p.position = emitterPos;
        p.position.x += (RandomFloat() - 0.5f) * 2.0f;
        p.position.z += (RandomFloat() - 0.5f) * 2.0f;

        p.velocity.x = (RandomFloat() - 0.5f) * m_particleSpeed;
        p.velocity.y = RandomFloat() * m_particleSpeed * 2.0f;
        p.velocity.z = (RandomFloat() - 0.5f) * m_particleSpeed;

        p.age = 0;
        p.lifetime = m_particleLifetime * (0.5f + RandomFloat() * 0.5f);
        p.color = m_particleColor;
        p.size = m_particleSize * (0.5f + RandomFloat());

        m_cpuParticles.push_back(p);
    }
}

void ParticleSystem::UpdateParticlesCPU(float deltaTime) {
    for (auto it = m_cpuParticles.begin(); it != m_cpuParticles.end(); ) {
        it->age += deltaTime / it->lifetime;
        it->position.x += it->velocity.x * deltaTime;
        it->position.y += it->velocity.y * deltaTime;
        it->position.z += it->velocity.z * deltaTime;
        it->velocity.y -= 2.0f * deltaTime; // Гравитация

        if (it->age >= 1.0f) {
            it = m_cpuParticles.erase(it);
        }
        else {
            ++it;
        }
    }
}

void ParticleSystem::Update(float deltaTime, const XMFLOAT3& emitterPosition) {
    if (!m_enabled) return;

    EmitParticles(deltaTime, emitterPosition);
    UpdateParticlesCPU(deltaTime);

    // Копируем в GPU буфер
    if (m_mappedParticles && !m_cpuParticles.empty()) {
        memcpy(m_mappedParticles, m_cpuParticles.data(),
            m_cpuParticles.size() * sizeof(CPUParticle));
    }

    // Обновляем константный буфер
    if (m_mappedCB) {
        ParticleCB* cb = (ParticleCB*)m_mappedCB;
        XMMATRIX viewProj = XMMatrixMultiply(m_viewMatrix, m_projMatrix);
        XMStoreFloat4x4(&cb->viewProjMatrix, XMMatrixTranspose(viewProj));
        cb->screenSize = XMFLOAT2(1920, 1080);
        cb->particleSize = m_particleSize;
    }
}

void ParticleSystem::Render(ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle) {
    if (!m_enabled || m_cpuParticles.empty()) return;

    // Устанавливаем render target (тот же, что и для сцены)
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    cmdList->SetPipelineState(m_renderPSO.Get());
    cmdList->SetGraphicsRootSignature(m_renderRootSig.Get());
    cmdList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootShaderResourceView(1, m_particleUploadBuffer->GetGPUVirtualAddress());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->DrawInstanced((UINT)m_cpuParticles.size(), 1, 0, 0);
}
#include "TAA.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <windows.h>
#include <d3dcompiler.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

    inline void ThrowIfFailed(HRESULT hr, const char* msg) {
        if (FAILED(hr)) {
            char buf[512];
            sprintf_s(buf, "TAA: %s (hr=0x%08X)\n", msg, hr);
            OutputDebugStringA(buf);
            throw std::runtime_error(buf);
        }
    }

    inline float Halton(int i, int b) {
        float f = 1.0f;
        float r = 0.0f;
        while (i > 0) {
            f /= (float)b;
            r += f * (float)(i % b);
            i /= b;
        }
        return r;
    }

}


void TAASystem::Initialize(ID3D12Device* device, UINT width, UINT height) {
    if (!device) throw std::runtime_error("TAA: null device");
    if (width == 0 || height == 0) throw std::runtime_error("TAA: zero size");

    m_width = width;
    m_height = height;

    BuildJitterSequence();
    CreateHistoryTextures(device, width, height);
    CreateHistoryRTVs(device);
    CreateConstantBuffer(device);
    CreateResolvePipeline(device);

    m_cbData.resolution = XMFLOAT2((float)width, (float)height);
    m_cbData.invResolution = XMFLOAT2(1.0f / (float)width, 1.0f / (float)height);
    m_cbData.blendFactor = 0.9f;
    m_cbData.historyValid = 0;
    UploadCB();

    m_jitterIndex = 0;
    m_currentHistory = 0;
    m_historyValid = false;

    char buf[256];
    sprintf_s(buf, "[TAA] Initialized (Step5). History: %ux%u RGBA16F\n",
        width, height);
    OutputDebugStringA(buf);
}

void TAASystem::Release() {
    if (m_cbResource && m_cbDataBegin) {
        m_cbResource->Unmap(0, nullptr);
        m_cbDataBegin = nullptr;
    }

    for (int i = 0; i < 2; ++i) m_historyColor[i].Reset();
    m_cbResource.Reset();
    m_historyRtvHeap.Reset();
    m_resolvePSO.Reset();
    m_resolveRootSig.Reset();

    m_historyValid = false;
    m_jitterIndex = 0;
    m_currentHistory = 0;
    OutputDebugStringA("[TAA] Released\n");
}


void TAASystem::BuildJitterSequence() {
    for (int i = 0; i < kJitterCount; ++i) {
        float h2 = Halton(i + 1, 2);
        float h3 = Halton(i + 1, 3);
        m_jitterSequence[i].x = h2 - 0.5f;
        m_jitterSequence[i].y = h3 - 0.5f;
    }
}

XMFLOAT2 TAASystem::GetJitterOffset(UINT /*frameIndex*/) {
    if (!m_enabled) return XMFLOAT2(0.0f, 0.0f);
    if (m_width == 0 || m_height == 0) return XMFLOAT2(0.0f, 0.0f);

    UINT idx = m_jitterIndex % kJitterCount;
    m_jitterIndex = (m_jitterIndex + 1) % kJitterCount;

    XMFLOAT2 j;
    j.x = m_jitterSequence[idx].x * 2.0f / (float)m_width;
    j.y = m_jitterSequence[idx].y * 2.0f / (float)m_height;
    return j;
}


void TAASystem::CreateHistoryTextures(ID3D12Device* device, UINT width, UINT height) {
    for (int i = 0; i < 2; ++i) m_historyColor[i].Reset();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearValue.Color[3] = 1.0f;

    for (int i = 0; i < 2; ++i) {
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
            IID_PPV_ARGS(&m_historyColor[i])), "CreateHistoryTexture");
    }
}

void TAASystem::CreateHistoryRTVs(ID3D12Device* device) {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = 2;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_historyRtvHeap)),
        "Create history RTV heap");

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE h =
        m_historyRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < 2; ++i) {
        m_historyRTV[i] = h;
        device->CreateRenderTargetView(m_historyColor[i].Get(), nullptr, h);
        h.ptr += rtvSize;
    }

    OutputDebugStringA("[TAA] History RTVs created\n");
}


void TAASystem::CreateConstantBuffer(ID3D12Device* device) {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(TAACB);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_cbResource)), "CreateTAACB");

    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(m_cbResource->Map(0, &readRange, &m_cbDataBegin), "MapTAACB");
}

void TAASystem::UploadCB() {
    if (!m_cbDataBegin) return;
    memcpy(m_cbDataBegin, &m_cbData, sizeof(TAACB));
}

void TAASystem::SavePrevFrame(const XMFLOAT4X4& viewProjNoJitter,
    const XMFLOAT3& cameraPos) {
    m_cbData.prevViewProj = viewProjNoJitter;
    m_cbData.prevCameraPos = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);
}


void TAASystem::CreateResolvePipeline(ID3D12Device* device) {

    static const char* vsCode = R"(
        struct VSOut {
            float4 pos : SV_POSITION;
            float2 uv  : TEXCOORD;
        };
        VSOut main(uint vid : SV_VertexID) {
            // Full-screen triangle: (-1,-1), (3,-1), (-1,3)
            float2 c = float2(
                (vid == 2) ? 3.0 : -1.0,
                (vid == 1) ? 3.0 : -1.0
            );
            VSOut o;
            o.pos = float4(c, 0.0, 1.0);
            o.uv  = float2((c.x + 1.0) * 0.5, 1.0 - (c.y + 1.0) * 0.5);
            return o;
        }
    )";

    static const char* psCode = R"(
        Texture2D<float4> CurrentColor : register(t0);
        Texture2D<float4> HistoryColor : register(t1);

        SamplerState LinearClamp : register(s0);

        cbuffer TAACB : register(b0) {
            float4x4 invViewProj;
            float4x4 prevViewProj;
            float4   cameraPos;
            float4   prevCameraPos;
            float2   resolution;
            float2   invResolution;
            float    blendFactor;
            int      historyValid;
            float2   _pad;
        };

        float2 ReprojectUV(float2 uv) {
            float2 ndc = uv * 2.0 - 1.0;
            ndc.y = -ndc.y;

            float depth = 0.5;
            float4 clip = float4(ndc, depth, 1.0);

            float4 world = mul(clip, invViewProj);
            world /= world.w;

            float4 prevClip = mul(world, prevViewProj);
            if (prevClip.w <= 1e-6) return uv;

            float2 prevNdc = prevClip.xy / prevClip.w;
            float2 prevUV  = prevNdc * 0.5 + 0.5;
            prevUV.y = 1.0 - prevUV.y;
            return prevUV;
        }

        float4 ClipToAABB(float4 history, float2 uv) {
            float3 minC = float3( 1e9,  1e9,  1e9);
            float3 maxC = float3(-1e9, -1e9, -1e9);

            [unroll]
            for (int y = -1; y <= 1; ++y) {
                [unroll]
                for (int x = -1; x <= 1; ++x) {
                    float2 o = float2(x, y) * invResolution;
                    float3 c = CurrentColor.SampleLevel(LinearClamp, uv + o, 0).rgb;
                    minC = min(minC, c);
                    maxC = max(maxC, c);
                }
            }

            float3 center = 0.5 * (minC + maxC);
            float3 extent = 0.5 * (maxC - minC) + 1e-5;
            float3 dir = history.rgb - center;
            float3 scale = abs(dir) / extent;
            float maxScale = max(scale.x, max(scale.y, scale.z));
            if (maxScale > 1.0) {
                history.rgb = center + dir / maxScale;
            }
            return history;
        }

        float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
            float4 current = CurrentColor.SampleLevel(LinearClamp, uv, 0);

            if (historyValid == 0) {
                return current;
            }

            float2 prevUV = ReprojectUV(uv);
            if (any(prevUV < 0.0) || any(prevUV > 1.0)) {
                return current;
            }

            float4 history = HistoryColor.SampleLevel(LinearClamp, prevUV, 0);
            history = ClipToAABB(history, uv);

            float blend = saturate(blendFactor);
            return lerp(current, history, blend);
        }
    )";

    ComPtr<ID3DBlob> vs, ps, err;
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), "TAA_ResolveVS",
        nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("TAA resolve VS compile failed");
    }

    hr = D3DCompile(psCode, strlen(psCode), "TAA_ResolvePS",
        nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("TAA resolve PS compile failed");
    }


    D3D12_DESCRIPTOR_RANGE rangeCurrent = {};
    rangeCurrent.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeCurrent.NumDescriptors = 1;
    rangeCurrent.BaseShaderRegister = 0;
    rangeCurrent.RegisterSpace = 0;
    rangeCurrent.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeHistory = {};
    rangeHistory.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeHistory.NumDescriptors = 1;
    rangeHistory.BaseShaderRegister = 1;
    rangeHistory.RegisterSpace = 0;
    rangeHistory.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &rangeCurrent;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &rangeHistory;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace = 0;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sig, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("TAA resolve RS serialize failed");
    }
    hr = device->CreateRootSignature(0, sig->GetBufferPointer(),
        sig->GetBufferSize(), IID_PPV_ARGS(&m_resolveRootSig));
    if (FAILED(hr)) throw std::runtime_error("TAA resolve RS create failed");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { nullptr, 0 };
    pso.pRootSignature = m_resolveRootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;

    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    pso.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_resolvePSO));
    if (FAILED(hr)) throw std::runtime_error("TAA resolve PSO create failed");

    OutputDebugStringA("[TAA] Resolve pipeline created\n");
}


void TAASystem::Resolve(ID3D12GraphicsCommandList* cmdList,
    ID3D12DescriptorHeap* srvHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE currentColorSRV)
{
    if (!m_resolvePSO || !m_resolveRootSig) return;
    if (!srvHeap) return;

    UINT srcIdx = m_currentHistory;
    UINT dstIdx = 1 - m_currentHistory;

    m_cbData.historyValid = m_historyValid ? 1 : 0;
    UploadCB();

    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_historyColor[dstIdx].Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &b);
    }

    cmdList->SetGraphicsRootSignature(m_resolveRootSig.Get());
    cmdList->SetPipelineState(m_resolvePSO.Get());

    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)m_width, (LONG)m_height };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_historyRTV[dstIdx];
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    cmdList->SetGraphicsRootConstantBufferView(
        2, m_cbResource->GetGPUVirtualAddress());

    cmdList->SetGraphicsRootDescriptorTable(0, currentColorSRV);

    cmdList->SetGraphicsRootDescriptorTable(1, m_historySRVGPU[srcIdx]);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_historyColor[dstIdx].Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &b);
    }

    m_currentHistory = dstIdx;
}
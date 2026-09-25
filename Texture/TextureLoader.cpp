#include "TextureLoader.h"
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "libs/d3dx12.h"

using namespace Microsoft::WRL;

#pragma pack(push, 1)
struct TGAHeader {
    uint8_t idLength;
    uint8_t colorMapType;
    uint8_t imageType;
    uint16_t colorMapOrigin;
    uint16_t colorMapLength;
    uint8_t colorMapDepth;
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t bitsPerPixel;
    uint8_t imageDescriptor;
};
#pragma pack(pop)

// Вспомогательная функция для проверки существования файла
static bool FileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.is_open();
}

// Загрузка TGA текстуры
static Texture LoadTGATexture(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::string& filename) {
    Texture texture;

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        OutputDebugStringA(("Failed to open TGA: " + filename + "\n").c_str());
        return TextureLoader::CreateDefaultTexture(device, commandList);
    }

    TGAHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(TGAHeader));

    if (header.imageType != 2 && header.imageType != 10) {
        OutputDebugStringA(("Unsupported TGA type: " + std::to_string(header.imageType) + "\n").c_str());
        return TextureLoader::CreateDefaultTexture(device, commandList);
    }

    bool isRLE = (header.imageType == 10);
    bool is32Bit = (header.bitsPerPixel == 32);
    bool is24Bit = (header.bitsPerPixel == 24);

    if (!is32Bit && !is24Bit) {
        OutputDebugStringA(("Unsupported TGA bpp: " + std::to_string(header.bitsPerPixel) + "\n").c_str());
        return TextureLoader::CreateDefaultTexture(device, commandList);
    }

    texture.width = header.width;
    texture.height = header.height;
    texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    int bytesPerPixel = header.bitsPerPixel / 8;
    int imageSize = texture.width * texture.height * bytesPerPixel;

    if (header.idLength > 0) {
        file.seekg(header.idLength, std::ios::cur);
    }

    std::vector<uint8_t> imageData(imageSize);

    if (isRLE) {
        int pixelCount = texture.width * texture.height;
        int currentPixel = 0;
        std::vector<uint8_t> pixelBuffer(bytesPerPixel);

        while (currentPixel < pixelCount) {
            uint8_t chunkHeader;
            file.read(reinterpret_cast<char*>(&chunkHeader), 1);

            if (chunkHeader < 128) {
                chunkHeader++;
                for (int i = 0; i < chunkHeader; i++) {
                    file.read(reinterpret_cast<char*>(pixelBuffer.data()), bytesPerPixel);
                    int idx = currentPixel * bytesPerPixel;
                    for (int j = 0; j < bytesPerPixel; j++) {
                        imageData[idx + j] = pixelBuffer[j];
                    }
                    currentPixel++;
                }
            }
            else {
                chunkHeader -= 127;
                file.read(reinterpret_cast<char*>(pixelBuffer.data()), bytesPerPixel);
                for (int i = 0; i < chunkHeader; i++) {
                    int idx = currentPixel * bytesPerPixel;
                    for (int j = 0; j < bytesPerPixel; j++) {
                        imageData[idx + j] = pixelBuffer[j];
                    }
                    currentPixel++;
                }
            }
        }
    }
    else {
        file.read(reinterpret_cast<char*>(imageData.data()), imageSize);
    }

    // Конвертируем BGR(A) в RGBA
    std::vector<uint8_t> rgbaData(texture.width * texture.height * 4);

    for (int i = 0; i < texture.width * texture.height; i++) {
        int srcIdx = i * bytesPerPixel;
        int dstIdx = i * 4;

        if (is32Bit) {
            rgbaData[dstIdx + 0] = imageData[srcIdx + 2];
            rgbaData[dstIdx + 1] = imageData[srcIdx + 1];
            rgbaData[dstIdx + 2] = imageData[srcIdx + 0];
            rgbaData[dstIdx + 3] = imageData[srcIdx + 3];
        }
        else {
            rgbaData[dstIdx + 0] = imageData[srcIdx + 2];
            rgbaData[dstIdx + 1] = imageData[srcIdx + 1];
            rgbaData[dstIdx + 2] = imageData[srcIdx + 0];
            rgbaData[dstIdx + 3] = 255;
        }
    }

    bool topLeft = (header.imageDescriptor & 0x20) != 0;

    if (!topLeft) {
        int rowSize = texture.width * 4;
        std::vector<uint8_t> flippedRow(rowSize);

        for (int y = 0; y < texture.height / 2; y++) {
            int topRow = y * rowSize;
            int bottomRow = (texture.height - 1 - y) * rowSize;

            memcpy(flippedRow.data(), &rgbaData[topRow], rowSize);
            memcpy(&rgbaData[topRow], &rgbaData[bottomRow], rowSize);
            memcpy(&rgbaData[bottomRow], flippedRow.data(), rowSize);
        }
    }

    // Создаём D3D12 текстуру
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = texture.width;
    texDesc.Height = texture.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = texture.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&texture.resource));

    if (FAILED(hr)) {
        OutputDebugStringA("Failed to create texture resource\n");
        return TextureLoader::CreateDefaultTexture(device, commandList);
    }

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&texture.uploadHeap));

    if (SUCCEEDED(hr)) {
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = rgbaData.data();
        textureData.RowPitch = texture.width * 4;
        textureData.SlicePitch = textureData.RowPitch * texture.height;

        UpdateSubresources(commandList, texture.resource.Get(), texture.uploadHeap.Get(),
            0, 0, 1, &textureData);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    char buffer[256];
    sprintf_s(buffer, "Loaded TGA: %s (%dx%d, %d bpp)\n",
        filename.c_str(), texture.width, texture.height, header.bitsPerPixel);
    OutputDebugStringA(buffer);

    return texture;
}

// Основной метод загрузки текстуры
Texture TextureLoader::LoadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::string& filename) {
    if (!FileExists(filename)) {
        OutputDebugStringA(("Texture file not found: " + filename + "\n").c_str());
        return CreateDefaultTexture(device, commandList);
    }

    // Определяем тип по расширению
    std::string ext = filename.substr(filename.find_last_of('.') + 1);
    for (auto& c : ext) c = tolower(c);

    if (ext == "tga") {
        return LoadTGATexture(device, commandList, filename);
    }

    // Используем stb_image для PNG, JPG, BMP
    int width, height, channels;
    unsigned char* imageData = stbi_load(filename.c_str(), &width, &height, &channels, 4);

    if (!imageData) {
        OutputDebugStringA(("stbi_load failed: " + filename + " - " + stbi_failure_reason() + "\n").c_str());
        return CreateDefaultTexture(device, commandList);
    }

    Texture texture;
    texture.width = width;
    texture.height = height;
    texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.filename = filename;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = texture.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&texture.resource));

    if (FAILED(hr)) {
        OutputDebugStringA("Failed to create texture resource\n");
        stbi_image_free(imageData);
        return CreateDefaultTexture(device, commandList);
    }

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&texture.uploadHeap));

    if (SUCCEEDED(hr)) {
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = imageData;
        textureData.RowPitch = width * 4;
        textureData.SlicePitch = textureData.RowPitch * height;

        UpdateSubresources(commandList, texture.resource.Get(), texture.uploadHeap.Get(),
            0, 0, 1, &textureData);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    stbi_image_free(imageData);

    char buffer[256];
    sprintf_s(buffer, "Loaded texture: %s (%dx%d)\n", filename.c_str(), width, height);
    OutputDebugStringA(buffer);

    return texture;
}

// Создание текстуры-заглушки
Texture TextureLoader::CreateDefaultTexture(ID3D12Device* device, ID3D12GraphicsCommandList* commandList) {
    Texture texture;
    texture.width = 1;
    texture.height = 1;
    texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = texture.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&texture.resource));

    if (SUCCEEDED(hr)) {
        uint32_t whitePixel = 0xFFFFFFFF;
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadBufferDesc = {};
        uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadBufferDesc.Width = uploadBufferSize;
        uploadBufferDesc.Height = 1;
        uploadBufferDesc.DepthOrArraySize = 1;
        uploadBufferDesc.MipLevels = 1;
        uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        uploadBufferDesc.SampleDesc.Count = 1;
        uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&texture.uploadHeap));

        if (SUCCEEDED(hr)) {
            D3D12_SUBRESOURCE_DATA texData = {};
            texData.pData = &whitePixel;
            texData.RowPitch = 4;
            texData.SlicePitch = 4;

            UpdateSubresources(commandList, texture.resource.Get(), texture.uploadHeap.Get(),
                0, 0, 1, &texData);

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture.resource.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
        }
    }

    OutputDebugStringA("Created default white texture\n");
    return texture;
}
//
// Created by Scott Harper on 5/1/2023.
//
// Reimplementing based on code from Microsoft:
// https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component
// https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12HelloWorld/src/HelloWindow
//

#ifndef SCRIBBLEKIT_INTERFACEDX12_H
#define SCRIBBLEKIT_INTERFACEDX12_H

#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <wrl/client.h>
#include "../GraphicsApiInterface.h"

#include <filesystem>

struct Float2 {
    float x;
    float y;
};

struct Float3 {
    float x;
    float y;
    float z;
};

struct Float4 {
    float x;
    float y;
    float z;
    float w;
};

namespace scribble {

    class InterfaceDX12 : public GraphicsApiInterface {
    public:
        void Init(GLFWwindow* p_window, std::string assets_path) override;
        void Update() override;
        void Render() override;
        void Destroy() override;

    private:
        static constexpr uint32_t FRAME_COUNT = 2;

        void LoadPipeline();
        void LoadAssets();
        void PopulateCommandList();
        void WaitForPreviousFrame();

        std::vector<uint8_t> GenerateTextureData();

        void GetHardwareAdapter(
                _In_ IDXGIFactory1* pFactory,
                _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter,
                bool requestHighPerformanceAdapter = false);

        std::wstring GetAssetFullPath(const std::string& asset_name)
        {
            auto asset_path = m_assetsPath;
            asset_path /= asset_name;
            return asset_path.wstring();
        }

        struct Vertex
        {
            Float3 position;
            Float2 uv;
        };

        struct SceneConstantBuffer
        {
            Float4 offset;
            float padding[60]; // padding to be 256-byte aligned
        };
        static_assert((sizeof(SceneConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

        // Use warp option

        bool m_use_warp_device = false;

        // Pipeline objects.

        D3D12_VIEWPORT m_viewport;
        D3D12_RECT m_scissorRect;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FRAME_COUNT];
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_bundleCommandAllocator;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap; // render target view
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap; // shader resource view
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_bundleCommandList;
        UINT m_rtvDescriptorSize;
        UINT m_srvDescriptorSize;

        // App resources.

        Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
        SceneConstantBuffer m_constantBufferData;
        UINT8* m_pCbvDataBegin; // TODO: pointers?! Yikes, don't do this...
        std::filesystem::path m_assetsPath;

        // Texture info

        static const UINT TextureWidth = 256;
        static const UINT TextureHeight = 256;
        static const UINT TexturePixelSize = 4;    // The number of bytes used to represent a pixel in the texture.

        // Synchronization objects.

        UINT m_frameIndex;
        HANDLE m_fenceEvent;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
        UINT64 m_fenceValue;

        // Window info/geometry

        float m_aspect_ratio;
        int m_width;
        int m_height;
        GLFWwindow* mp_window;
    };

} // scribble

#endif //SCRIBBLEKIT_INTERFACEDX12_H

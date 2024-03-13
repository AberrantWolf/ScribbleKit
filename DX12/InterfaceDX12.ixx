//
// Created by lareo on 2024 March 13.
//

module;


#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <wrl/client.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <d3dcompiler.h>
#include <GLFW/glfw3native.h>

#include <filesystem>
#include <iostream>

export module InterfaceDX12;

import types;
import GraphicsApiInterface;

using namespace Microsoft::WRL;

template<typename T>
void ThrowIfFailed(T value, const std::string &err_message) {
    if (value != S_OK) {
        throw std::runtime_error(err_message + " " + std::to_string(value));
    }
}

template<typename T>
void ThrowIfFailed(T value) {
    ThrowIfFailed(value, "Unspecified failure");
}

namespace scribble {
    export class InterfaceDX12 : public GraphicsApiInterface {
    public:
        void Init(GLFWwindow *p_window, const std::string &assets_path) override {
            mp_window = p_window;
            std::cout << "Window set to " << mp_window << std::endl;

            m_assetsPath = std::filesystem::path(assets_path);

            m_frameIndex = 0;
            glfwGetWindowSize(mp_window, &m_width, &m_height);
            m_aspect_ratio = float(m_width) / float(m_height);
            m_viewport = D3D12_VIEWPORT{0.f, 0.f, float(m_width), float(m_height)};
            m_scissorRect = tagRECT{0, 0, long(m_width), long(m_height)};
            m_rtvDescriptorSize = 0;

            m_constantBufferData = SceneConstantBuffer{};

            LoadPipeline();
            printf("...Pipeline loaded\n");

            LoadAssets();
            printf("...Assets loaded\n");
        }

        void Update() override {
            const float translationSpeed = 0.005f;
            const float offsetBounds = 1.25f;

            m_constantBufferData.offset.x += translationSpeed;
            if (m_constantBufferData.offset.x > offsetBounds) {
                m_constantBufferData.offset.x = -offsetBounds;
            }
            memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
        }

        void Render() override {
            // Record all the commands we need to render the scene into the command list.
            PopulateCommandList();

            // Execute the command list.
            ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Present the frame.
            ThrowIfFailed(m_swapChain->Present(1, 0), "Presenting swapchain");

            WaitForPreviousFrame();
        }

        void Destroy() override {
            // Ensure that the GPU is no longer referencing resources that are about to be
            // cleaned up by the destructor.
            WaitForPreviousFrame();

            CloseHandle(m_fenceEvent);
        }

    private:
        static constexpr uint32_t FRAME_COUNT = 2;

        void LoadPipeline() {
            UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
            ComPtr<ID3D12Debug1> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
                debugController->SetEnableGPUBasedValidation(true);
                debugController->SetEnableSynchronizedCommandQueueValidation(true);
                printf("...debug layer enabled\n");
                fprintf(stderr, "Test");
            }
#endif

            ComPtr<IDXGIFactory4> factory;
            ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)), "Error creating DXGI factory");
            printf("...dxgi factory created\n");

            if (m_use_warp_device) {
                ComPtr<IDXGIAdapter> warpAdapter;
                ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

                ThrowIfFailed(D3D12CreateDevice(
                    warpAdapter.Get(),
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&m_device)
                ), "Creating warp device object");
            } else {
                ComPtr<IDXGIAdapter1> hardwareAdapter;
                GetHardwareAdapter(factory.Get(), &hardwareAdapter);

                ThrowIfFailed(D3D12CreateDevice(
                    hardwareAdapter.Get(),
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&m_device)), "Creating device object");
            }
            printf("...adapter found\n");

            // Describe and create a command queue
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            // TODO: learn more about the different command list types

            ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)),
                "Creating command queue");
            printf("...command queue created\n");

            // Next up is a swap chain
            // TODO: Use a (more modern) Composition Swapchain instead
            // https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain-portal
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChainDesc.BufferCount = FRAME_COUNT;
            swapChainDesc.Width = m_width;
            swapChainDesc.Height = m_height;
            swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.SampleDesc.Count = 1;

            ComPtr<IDXGISwapChain1> swapChain;
            ThrowIfFailed(factory->CreateSwapChainForHwnd(
                m_commandQueue.Get(), // Swap chain needs the queue so that it can force a flush on it.
                glfwGetWin32Window(mp_window),
                &swapChainDesc,
                nullptr,
                nullptr,
                &swapChain
            ), "Creating swapchain");
            printf("...swapchain created\n");

            // This sample does not support fullscreen transitions.
            ThrowIfFailed(factory->MakeWindowAssociation(glfwGetWin32Window(mp_window), DXGI_MWA_NO_ALT_ENTER),
                "Making window association");

            ThrowIfFailed(swapChain.As(&m_swapChain), "Assigning member swapchain");
            m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

            // Create descriptor heaps.
            {
                // Describe and create a render target view (RTV) descriptor heap.
                D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
                rtvHeapDesc.NumDescriptors = FRAME_COUNT;
                rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)),
                    "Creating descriptor heap RTV");
                m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

                // Describe and create a shader resource view (SRV) heap for the texture.
                D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
                srvHeapDesc.NumDescriptors = 2;
                srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)),
                    "Creating descriptor heap SRV");
                m_srvDescriptorSize = m_device->
                    GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                // Describe and create a constant buffer view (CBV) descriptor heap.
                // Flags indicate that this descriptor heap can be bound to the pipeline
                // and that descriptors contained in it can be referenced by a root table.
                //            D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
                //            cbvHeapDesc.NumDescriptors = 1;
                //            cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                //            cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                //            ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
            }

            // Create frame resources.
            {
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

                // Create a RTV for each frame.
                for (UINT n = 0; n < FRAME_COUNT; n++) {
                    ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])),
                        "Getting swap chain buffer");
                    m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
                    rtvHandle.Offset(1, m_rtvDescriptorSize);
                }
            }

            ThrowIfFailed(
                m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)),
                "Creating command allocator");
            ThrowIfFailed(
                m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE,
                    IID_PPV_ARGS(&m_bundleCommandAllocator)),
                "Creating bundle command allocator");
        }

        void LoadAssets() {
            // Create an empty root signature.
            // TODO: What is a root signature?
            {
                D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

                // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
                if (FAILED(
                    m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
                    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
                }


                CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
                ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
                ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

                CD3DX12_ROOT_PARAMETER1 rootParameters[2];
                rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // t0
                rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_VERTEX); // b0

                D3D12_STATIC_SAMPLER_DESC sampler = {};
                sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                sampler.MipLODBias = 0;
                sampler.MaxAnisotropy = 0;
                sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
                sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
                sampler.MinLOD = 0.0f;
                sampler.MaxLOD = D3D12_FLOAT32_MAX;
                sampler.ShaderRegister = 0;
                sampler.RegisterSpace = 0;
                sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                // Allow input layout and deny unnecessary access to certain pipeline stages.
                D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
                    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; // |
                //                    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                //                    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                //                    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;// |
                //                    D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

                CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
                rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);

                ComPtr<ID3DBlob> signature;
                ComPtr<ID3DBlob> error;
                ThrowIfFailed(
                    D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature,
                        &error), "Serializing root signature");
                ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                        signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                    "Creating root signature");
            }

            // Create the pipeline state, which includes compiling and loading shaders.
            {
                ComPtr<ID3DBlob> vertexShader;
                ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
                // Enable better shader debugging with the graphics debugging tools.
                UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
                UINT compileFlags = 0;
#endif

                ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath("shaders.hlsl").c_str(), nullptr, nullptr, "VSMain",
                        "vs_5_0", compileFlags, 0, &vertexShader, nullptr),
                    "Compiling vertex shader");
                ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath("shaders.hlsl").c_str(), nullptr, nullptr, "PSMain",
                        "ps_5_0", compileFlags, 0, &pixelShader, nullptr),
                    "Compiling pixel shader");

                // Define the vertex input layout.
                D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
                {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
                };

                // Describe and create the graphics pipeline state object (PSO).
                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
                psoDesc.pRootSignature = m_rootSignature.Get();
                psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
                psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
                psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                psoDesc.DepthStencilState.DepthEnable = FALSE;
                psoDesc.DepthStencilState.StencilEnable = FALSE;
                psoDesc.SampleMask = UINT_MAX;
                psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                psoDesc.NumRenderTargets = 1;
                psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                psoDesc.SampleDesc.Count = 1;
                ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)),
                    "Creating PSO");
            }

            // Create the command list.
            ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(),
                    nullptr, IID_PPV_ARGS(&m_commandList)),
                "Creating command list in assets");

            // Create the vertex buffer.
            {
                // Define the geometry for a triangle.
                Vertex triangleVertices[] =
                {
                    {{0.0f, 0.25f * m_aspect_ratio, 0.0f}, {0.5f, 0.0f}},
                    {{0.25f, -0.25f * m_aspect_ratio, 0.0f}, {1.0f, 1.0f}},
                    {{-0.25f, -0.25f * m_aspect_ratio, 0.0f}, {0.0f, 1.0f}}
                };

                const UINT vertexBufferSize = sizeof(triangleVertices);

                // Note: using upload heaps to transfer static data like vert buffers is not
                // recommended. Every time the GPU needs it, the upload heap will be marshalled
                // over. Please read up on Default Heap usage. An upload heap is used here for
                // code simplicity and because there are very few verts to actually transfer.
                {
                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                    const auto vb_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
                    ThrowIfFailed(m_device->CreateCommittedResource(
                        &heap_properties,
                        D3D12_HEAP_FLAG_NONE,
                        &vb_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&m_vertexBuffer)), "Creating committed resource for vertex buffer");
                }
                // Copy the triangle data to the vertex buffer.
                UINT8 *pVertexDataBegin;
                CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
                ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)),
                    "Mapping the vertex buffer");
                memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
                m_vertexBuffer->Unmap(0, nullptr);

                // Initialize the vertex buffer view.
                m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
                m_vertexBufferView.StrideInBytes = sizeof(Vertex);
                m_vertexBufferView.SizeInBytes = vertexBufferSize;
            }

            // Create and record the bundle.
            {
                ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
                        m_bundleCommandAllocator.Get(), m_pipelineState.Get(),
                        IID_PPV_ARGS(&m_bundleCommandList)),
                    "Creating bundle command list");
                m_bundleCommandList->SetGraphicsRootSignature(m_rootSignature.Get());
                m_bundleCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                m_bundleCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
                m_bundleCommandList->DrawInstanced(3, 1, 0, 0);
                ThrowIfFailed(m_bundleCommandList->Close(), "Closing bundle command list");
            }

            // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
            // the command list that references it has finished executing on the GPU.
            // We will flush the GPU at the end of this method to ensure the resource is not
            // prematurely destroyed.
            ComPtr<ID3D12Resource> textureUploadHeap;

            // Create the texture.
            {
                // Describe and create a Texture2D.
                D3D12_RESOURCE_DESC textureDesc = {};
                textureDesc.MipLevels = 1;
                textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                textureDesc.Width = TextureWidth;
                textureDesc.Height = TextureHeight;
                textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
                textureDesc.DepthOrArraySize = 1;
                textureDesc.SampleDesc.Count = 1;
                textureDesc.SampleDesc.Quality = 0;
                textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; {
                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                    ThrowIfFailed(m_device->CreateCommittedResource(
                            &heap_properties,
                            D3D12_HEAP_FLAG_NONE,
                            &textureDesc,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            nullptr,
                            IID_PPV_ARGS(&m_texture)),
                        "Creating committed resource for the texture"
                    );
                }

                {
                    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                    const auto buffer_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
                    // Create the GPU upload buffer.
                    ThrowIfFailed(m_device->CreateCommittedResource(
                        &heap_properties,
                        D3D12_HEAP_FLAG_NONE,
                        &buffer_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&textureUploadHeap)), "Creating committed resource for texture");
                }

                // Copy data to the intermediate upload heap and then schedule a copy
                // from the upload heap to the Texture2D.
                std::vector<UINT8> texture = GenerateTextureData();

                D3D12_SUBRESOURCE_DATA textureData = {};
                textureData.pData = &texture[0];
                textureData.RowPitch = TextureWidth * TexturePixelSize;
                textureData.SlicePitch = textureData.RowPitch * TextureHeight;

                UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1,
                    &textureData);

                const auto barriers = CD3DX12_RESOURCE_BARRIER::Transition(
                    m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                m_commandList->ResourceBarrier(1, &barriers);

                // Describe and create an SRV for the texture.
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = textureDesc.Format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                CD3DX12_CPU_DESCRIPTOR_HANDLE descHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());
                m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, descHandle);
            }

            // Create the constant buffer.
            {
                const UINT constantBufferSize = sizeof(SceneConstantBuffer);
                // CB size is required to be 256-byte aligned.

                {
                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                    const auto buffer_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);
                    ThrowIfFailed(m_device->CreateCommittedResource(
                        &heap_properties,
                        D3D12_HEAP_FLAG_NONE,
                        &buffer_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&m_constantBuffer)));
                }

                // Describe and create a constant buffer view.
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
                cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
                cbvDesc.SizeInBytes = constantBufferSize;
                CD3DX12_CPU_DESCRIPTOR_HANDLE descHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), 1,
                    INT(m_srvDescriptorSize));
                m_device->CreateConstantBufferView(&cbvDesc, descHandle);

                // Map and initialize the constant buffer. We don't unmap this until the
                // app closes. Keeping things mapped for the lifetime of the resource is okay.
                CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
                ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void **>(&m_pCbvDataBegin)));
                memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
            }

            // Close the command list and execute it to begin the initial GPU setup.
            ThrowIfFailed(m_commandList->Close(), "Closing command list in assets after texture");
            ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Create synchronization objects.
            {
                ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
                    "Creating fence in assets");
                m_fenceValue = 1;

                // Create an event handle to use for frame synchronization.
                m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (m_fenceEvent == nullptr) {
                    ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
                }

                // Wait for the command list to execute; we are reusing the same command
                // list in our main loop but for now, we just want to wait for setup to
                // complete before continuing.
                WaitForPreviousFrame();
            }
        }

        void PopulateCommandList() {
            // Command list allocators can only be reset when the associated
            // command lists have finished execution on the GPU; apps should use
            // fences to determine GPU execution progress.
            ThrowIfFailed(m_commandAllocator->Reset(), "Resetting command allocator");

            // However, when ExecuteCommandList() is called on a particular command
            // list, that command list can then be reset at any time and must be before
            // re-recording.
            ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()),
                "Resetting command list");

            // Set necessary state.
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

            // TODO: The descriptor heap stuff seems to be required in order for the shader object to have access to the texture data uploaded previously
            // TODO: Can this stuff be moved up to inside the bundle rather than set here each time?
            ID3D12DescriptorHeap *ppHeaps[] = {m_srvHeap.Get()};
            m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

            m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
            CD3DX12_GPU_DESCRIPTOR_HANDLE hdl(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), m_srvDescriptorSize);
            m_commandList->SetGraphicsRootDescriptorTable(1, hdl);
            m_commandList->RSSetViewports(1, &m_viewport);
            m_commandList->RSSetScissorRects(1, &m_scissorRect);

            // Indicate that the back buffer will be used as a render target.
            {
                const auto barriers = CD3DX12_RESOURCE_BARRIER::Transition(
                    m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                m_commandList->ResourceBarrier(1, &barriers);
            }

            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex,
                m_rtvDescriptorSize);
            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

            // Record commands.
            const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

            // Execute the commands stored in the bundle.
            m_commandList->ExecuteBundle(m_bundleCommandList.Get());

            //        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            //        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
            //        m_commandList->DrawInstanced(3, 1, 0, 0);

            // Indicate that the back buffer will now be used to present.
            {
                const auto barriers = CD3DX12_RESOURCE_BARRIER::Transition(
                    m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT);
                m_commandList->ResourceBarrier(1, &barriers);
            }

            ThrowIfFailed(m_commandList->Close(), "Closing command list");
        }

        void WaitForPreviousFrame() {
            // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
            // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
            // sample illustrates how to use fences for efficient resource usage and to
            // maximize GPU utilization.

            // Signal and increment the fence value.
            const UINT64 fence = m_fenceValue;
            ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
            m_fenceValue++;

            // Wait until the previous frame is finished.
            if (m_fence->GetCompletedValue() < fence) {
                ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }

            m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        }

        std::vector<uint8_t> GenerateTextureData() {
            printf("...Generating texture data...\n");
            const UINT rowPitch = TextureWidth * TexturePixelSize;
            const UINT cellPitch = rowPitch >> 3; // The width of a cell in the checkboard texture.
            const UINT cellHeight = TextureWidth >> 3; // The height of a cell in the checkerboard texture.
            const UINT textureSize = rowPitch * TextureHeight;

            std::vector<UINT8> data(textureSize);
            UINT8 *pData = &data[0];

            for (UINT n = 0; n < textureSize; n += TexturePixelSize) {
                UINT x = n % rowPitch;
                UINT y = n / rowPitch;
                UINT i = x / cellPitch;
                UINT j = y / cellHeight;

                if (i % 2 == j % 2) {
                    pData[n] = 0x00; // R
                    pData[n + 1] = 0x00; // G
                    pData[n + 2] = 0x00; // B
                    pData[n + 3] = 0xff; // A
                } else {
                    pData[n] = 0xff; // R
                    pData[n + 1] = 0xff; // G
                    pData[n + 2] = 0xff; // B
                    pData[n + 3] = 0xff; // A
                }
            }

            return data;
        }

        void GetHardwareAdapter(
            _In_ IDXGIFactory1 *pFactory,
            _Outptr_result_maybenull_ IDXGIAdapter1 **ppAdapter,
            bool requestHighPerformanceAdapter = false) {
            *ppAdapter = nullptr;

            ComPtr<IDXGIAdapter1> adapter;

            // Try to get an adapter using GPU preference first, which requires an IDXGIFactory6 instance
            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
                for (
                    size_t adapterIndex = 0;
                    SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                        adapterIndex,
                        requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE :
                        DXGI_GPU_PREFERENCE_UNSPECIFIED,
                        IID_PPV_ARGS(&adapter)));
                    ++adapterIndex) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                        continue;
                    }

                    if (SUCCEEDED(D3D12CreateDevice(
                        adapter.Get(),
                        D3D_FEATURE_LEVEL_12_0,
                        _uuidof(ID3D12Device),
                        nullptr))) {
                        // Found a compatible adapter, and it's already been assigned (above)
                        break;
                    }
                }
            }

            // If no adapter was found using GPU preference, try just normally without
            if (adapter.Get() == nullptr) {
                for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++
                     adapterIndex) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                        // Don't select the Basic Render Driver adapter.
                        // If you want a software adapter, pass in "/warp" on the command line.
                        continue;
                    }

                    // Check to see whether the adapter supports Direct3D 12, but don't create the
                    // actual device yet.
                    if (SUCCEEDED(D3D12CreateDevice(
                        adapter.Get(),
                        D3D_FEATURE_LEVEL_11_0,
                        _uuidof(ID3D12Device),
                        nullptr))) {
                        // Found a compatible adapter, and it's already been assigned (above)
                        break;
                    }
                }
            }

            *ppAdapter = adapter.Detach();
        }

        std::wstring GetAssetFullPath(const std::string &asset_name) {
            auto asset_path = m_assetsPath;
            asset_path /= asset_name;
            return asset_path.wstring();
        }

        struct Vertex {
            float3 position;
            float2 uv;
        };

        struct SceneConstantBuffer {
            float4 offset;
            float padding[60]; // padding to be 256-byte aligned
        };

        static_assert((sizeof(SceneConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

        // Use warp option

        bool m_use_warp_device = false;

        // Pipeline objects.

        D3D12_VIEWPORT m_viewport{};
        D3D12_RECT m_scissorRect{};
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
        UINT m_rtvDescriptorSize{};
        UINT m_srvDescriptorSize{};

        // App resources.

        Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
        Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
        SceneConstantBuffer m_constantBufferData{};
        UINT8 *m_pCbvDataBegin{}; // TODO: pointers?! Yikes, don't do this...
        std::filesystem::path m_assetsPath;

        // Texture info

        static const UINT TextureWidth = 256;
        static const UINT TextureHeight = 256;
        static const UINT TexturePixelSize = 4; // The number of bytes used to represent a pixel in the texture.

        // Synchronization objects.

        UINT m_frameIndex{};
        HANDLE m_fenceEvent{};
        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
        UINT64 m_fenceValue{};

        // Window info/geometry

        float m_aspect_ratio{};
        int m_width{};
        int m_height{};
        GLFWwindow *mp_window{};
    };

    export GraphicsApiInterface *MakeGraphicsApi() {
        return new InterfaceDX12();
    }
} // scribble

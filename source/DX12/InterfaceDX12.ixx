//
// Created by Scott Harper on 2024 March 13.
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

#include <fmt/core.h>

export module scribble.graphics.dx12;

import scribble.types;
import scribble.graphics;

using namespace Microsoft::WRL;

template<typename T>
void throw_if_failed(T value, const std::string &err_message) {
    if (value != S_OK) {
        throw std::runtime_error(err_message + " " + std::to_string(value));
    }
}

template<typename T>
void throw_if_failed(T value) {
    throw_if_failed(value, "Unspecified failure");
}

namespace scribble::dx12 {
    export class dx12_interface final : public graphics_interface {
    public:
        void init(GLFWwindow *p_window, const std::string &assets_path) override {
            p_window_ = p_window;
            fmt::println("Window set to {}", fmt::ptr(p_window_));

            assets_path_ = std::filesystem::path(assets_path);

            frame_index_ = 0;
            glfwGetWindowSize(p_window_, &width_, &height_);

            const auto f_width = static_cast<float>(width_);
            const auto f_height = static_cast<float>(height_);

            aspect_ratio_ = f_width / f_height;
            viewport_ = D3D12_VIEWPORT{0.f, 0.f, f_width, f_height};
            scissorRect_ = tagRECT{0, 0, long(width_), long(height_)};
            rtv_descriptor_size_ = 0;

            constant_buffer_data_ = SceneConstantBuffer{};

            load_pipeline();
            fmt::println("...Pipeline loaded");

            load_assets();
            fmt::println("...Assets loaded");
        }

        void update() override {
	        constexpr float translation_speed = 0.005f;
	        constexpr float offset_bounds = 1.25f;

            constant_buffer_data_.offset.x += translation_speed;
            if (constant_buffer_data_.offset.x > offset_bounds) {
                constant_buffer_data_.offset.x = -offset_bounds;
            }
            memcpy(p_cbv_data_begin_, &constant_buffer_data_, sizeof(constant_buffer_data_));
        }

        void render() override {
            // Record all the commands we need to render the scene into the command list.
            populate_command_list();

            // Execute the command list.
            ID3D12CommandList *ppCommandLists[] = {command_list_.Get()};
            command_queue_->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Present the frame.
            throw_if_failed(swap_chain_->Present(1, 0), "Presenting swapchain");

            wait_for_previous_frame();
        }

        void destroy() override {
            // Ensure that the GPU is no longer referencing resources that are about to be
            // cleaned up by the destructor.
            wait_for_previous_frame();

            CloseHandle(fence_event_);
        }

    private:
        static constexpr uint32_t frame_count = 2;

        void load_pipeline() {
	        constexpr UINT dxgi_factory_flags = 0;

#if defined(_DEBUG)
            ComPtr<ID3D12Debug1> debug_controller;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
                debug_controller->EnableDebugLayer();
                debug_controller->SetEnableGPUBasedValidation(true);
                debug_controller->SetEnableSynchronizedCommandQueueValidation(true);
                printf("...debug layer enabled\n");
                fprintf(stderr, "Test");
            }
#endif

            ComPtr<IDXGIFactory4> factory;
            throw_if_failed(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)), "Error creating DXGI factory");
            fmt::println("...dxgi factory created\n");

            if (m_use_warp_device) {
                ComPtr<IDXGIAdapter> warpAdapter;
                throw_if_failed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

                throw_if_failed(D3D12CreateDevice(
                    warpAdapter.Get(),
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device_)
                ), "Creating warp device object");
            } else {
                ComPtr<IDXGIAdapter1> hardware_adapter;
                get_hardware_adapter(factory.Get(), &hardware_adapter);

                throw_if_failed(D3D12CreateDevice(
                    hardware_adapter.Get(),
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device_)), "Creating device object");
            }
            fmt::println("...adapter found\n");

            // Describe and create a command queue
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            // TODO: learn more about the different command list types

            throw_if_failed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
                "Creating command queue");
            fmt::println("...command queue created\n");

            // Next up is a swap chain
            // TODO: Use a (more modern) Composition Swapchain instead
            // https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain-portal
            DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
            swap_chain_desc.BufferCount = frame_count;
            swap_chain_desc.Width = width_;
            swap_chain_desc.Height = height_;
            swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swap_chain_desc.SampleDesc.Count = 1;

            ComPtr<IDXGISwapChain1> swap_chain;
            throw_if_failed(factory->CreateSwapChainForHwnd(
                command_queue_.Get(), // Swap chain needs the queue so that it can force a flush on it.
                glfwGetWin32Window(p_window_),
                &swap_chain_desc,
                nullptr,
                nullptr,
                &swap_chain
            ), "Creating swapchain");
            fmt::println("...swapchain created\n");

            // This sample does not support fullscreen transitions.
            throw_if_failed(factory->MakeWindowAssociation(glfwGetWin32Window(p_window_), DXGI_MWA_NO_ALT_ENTER),
                "Making window association");

            throw_if_failed(swap_chain.As(&swap_chain_), "Assigning member swapchain");
            frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

            // Create descriptor heaps.
            {
                // Describe and create a render target view (RTV) descriptor heap.
                D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
                rtv_heap_desc.NumDescriptors = frame_count;
                rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                throw_if_failed(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)),
                    "Creating descriptor heap RTV");
                rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

                // Describe and create a shader resource view (SRV) heap for the texture.
                D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
                srv_heap_desc.NumDescriptors = 2;
                srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                throw_if_failed(device_->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap_)),
                    "Creating descriptor heap SRV");
                srv_descriptor_size_ = device_->
                    GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                // Describe and create a constant buffer view (CBV) descriptor heap.
                // Flags indicate that this descriptor heap can be bound to the pipeline
                // and that descriptors contained in it can be referenced by a root table.
                //            D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
                //            cbvHeapDesc.NumDescriptors = 1;
                //            cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                //            cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                //            ThrowIfFailed(device_->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
            }

            // Create frame resources.
            {
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap_->GetCPUDescriptorHandleForHeapStart());

                // Create an RTV for each frame.
                for (UINT n = 0; n < frame_count; n++) {
                    throw_if_failed(swap_chain_->GetBuffer(n, IID_PPV_ARGS(&render_targets_[n])),
                        "Getting swap chain buffer");
                    device_->CreateRenderTargetView(render_targets_[n].Get(), nullptr, rtv_handle);
                    rtv_handle.Offset(1, rtv_descriptor_size_);
                }
            }

            throw_if_failed(
                device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator_)),
                "Creating command allocator");
            throw_if_failed(
                device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE,
                    IID_PPV_ARGS(&bundle_command_allocator_)),
                "Creating bundle command allocator");
        }

        void load_assets() {
            // Create an empty root signature.
            // TODO: What is a root signature?
            {
                D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

                // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
                if (FAILED(
                    device_->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
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

                CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
                root_signature_desc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);

                ComPtr<ID3DBlob> signature;
                ComPtr<ID3DBlob> error;
                throw_if_failed(
                    D3DX12SerializeVersionedRootSignature(&root_signature_desc, featureData.HighestVersion, &signature,
                        &error), "Serializing root signature");
                throw_if_failed(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                        signature->GetBufferSize(), IID_PPV_ARGS(&root_signature_)),
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

                throw_if_failed(D3DCompileFromFile(get_asset_full_path("shaders.hlsl").c_str(), nullptr, nullptr, "VSMain",
                        "vs_5_0", compileFlags, 0, &vertexShader, nullptr),
                    "Compiling vertex shader");
                throw_if_failed(D3DCompileFromFile(get_asset_full_path("shaders.hlsl").c_str(), nullptr, nullptr, "PSMain",
                        "ps_5_0", compileFlags, 0, &pixelShader, nullptr),
                    "Compiling pixel shader");

                // Define the vertex input layout.
                D3D12_INPUT_ELEMENT_DESC input_element_descs[] =
                {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
                };

                // Describe and create the graphics pipeline state object (PSO).
                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.InputLayout = {input_element_descs, _countof(input_element_descs)};
                psoDesc.pRootSignature = root_signature_.Get();
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
                throw_if_failed(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline_state_)),
                    "Creating PSO");
            }

            // Create the command list.
            throw_if_failed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator_.Get(),
                    nullptr, IID_PPV_ARGS(&command_list_)),
                "Creating command list in assets");

            // Create the vertex buffer.
            {
                // Define the geometry for a triangle.
                Vertex triangle_vertices[] =
                {
                    {{0.0f, 0.25f * aspect_ratio_, 0.0f}, {0.5f, 0.0f}},
                    {{0.25f, -0.25f * aspect_ratio_, 0.0f}, {1.0f, 1.0f}},
                    {{-0.25f, -0.25f * aspect_ratio_, 0.0f}, {0.0f, 1.0f}}
                };

                constexpr UINT vertex_buffer_size = sizeof(triangle_vertices);

                // Note: using upload heaps to transfer static data like vert buffers is not
                // recommended. Every time the GPU needs it, the upload heap will be marshalled
                // over. Please read up on Default Heap usage. An upload heap is used here for
                // code simplicity and because there are very few verts to actually transfer.
                {
                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                    const auto vb_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size);
                    throw_if_failed(device_->CreateCommittedResource(
                        &heap_properties,
                        D3D12_HEAP_FLAG_NONE,
                        &vb_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&vertex_buffer_)), "Creating committed resource for vertex buffer");
                }
                // Copy the triangle data to the vertex buffer.
                UINT8 *pVertexDataBegin;
                CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
                throw_if_failed(vertex_buffer_->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)),
                    "Mapping the vertex buffer");
                memcpy(pVertexDataBegin, triangle_vertices, sizeof(triangle_vertices));
                vertex_buffer_->Unmap(0, nullptr);

                // Initialize the vertex buffer view.
                vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
                vertex_buffer_view_.StrideInBytes = sizeof(Vertex);
                vertex_buffer_view_.SizeInBytes = vertex_buffer_size;
            }

            // Create and record the bundle.
            {
                throw_if_failed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
                        bundle_command_allocator_.Get(), pipeline_state_.Get(),
                        IID_PPV_ARGS(&bundle_command_list_)),
                    "Creating bundle command list");
                bundle_command_list_->SetGraphicsRootSignature(root_signature_.Get());
                bundle_command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                bundle_command_list_->IASetVertexBuffers(0, 1, &vertex_buffer_view_);
                bundle_command_list_->DrawInstanced(3, 1, 0, 0);
                throw_if_failed(bundle_command_list_->Close(), "Closing bundle command list");
            }

            // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
            // the command list that references it has finished executing on the GPU.
            // We will flush the GPU at the end of this method to ensure the resource is not
            // prematurely destroyed.
            ComPtr<ID3D12Resource> texture_upload_heap;

            // Create the texture.
            {
                // Describe and create a Texture2D.
                D3D12_RESOURCE_DESC texture_desc = {};
                texture_desc.MipLevels = 1;
                texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                texture_desc.Width = texture_width;
                texture_desc.Height = texture_height;
                texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
                texture_desc.DepthOrArraySize = 1;
                texture_desc.SampleDesc.Count = 1;
                texture_desc.SampleDesc.Quality = 0;
                texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; {
                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                    throw_if_failed(device_->CreateCommittedResource(
                            &heap_properties,
                            D3D12_HEAP_FLAG_NONE,
                            &texture_desc,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            nullptr,
                            IID_PPV_ARGS(&texture_)),
                        "Creating committed resource for the texture"
                    );
                }

                {
                    const UINT64 upload_buffer_size = GetRequiredIntermediateSize(texture_.Get(), 0, 1);

                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                    const auto buffer_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(upload_buffer_size);
                    // Create the GPU upload buffer.
                    throw_if_failed(device_->CreateCommittedResource(
                        &heap_properties,
                        D3D12_HEAP_FLAG_NONE,
                        &buffer_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&texture_upload_heap)), "Creating committed resource for texture");
                }

                // Copy data to the intermediate upload heap and then schedule a copy
                // from the upload heap to the Texture2D.
                std::vector<UINT8> texture = generate_texture_data();

                D3D12_SUBRESOURCE_DATA textureData = {};
                textureData.pData = &texture[0];
                textureData.RowPitch = texture_width * texture_pixel_size;
                textureData.SlicePitch = textureData.RowPitch * texture_height;

                UpdateSubresources(command_list_.Get(), texture_.Get(), texture_upload_heap.Get(), 0, 0, 1,
                    &textureData);

                const auto barriers = CD3DX12_RESOURCE_BARRIER::Transition(
                    texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                command_list_->ResourceBarrier(1, &barriers);

                // Describe and create an SRV for the texture.
                D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv_desc.Format = texture_desc.Format;
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MipLevels = 1;
                CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle(srv_heap_->GetCPUDescriptorHandleForHeapStart());
                device_->CreateShaderResourceView(texture_.Get(), &srv_desc, desc_handle);
            }

            // Create the constant buffer.
            {
	            constexpr UINT constant_buffer_size = sizeof(SceneConstantBuffer);
                // CB size is required to be 256-byte aligned.

                {
                    const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                    const auto buffer_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(constant_buffer_size);
                    throw_if_failed(device_->CreateCommittedResource(
                        &heap_properties,
                        D3D12_HEAP_FLAG_NONE,
                        &buffer_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&constant_buffer_)));
                }

                // Describe and create a constant buffer view.
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
                cbv_desc.BufferLocation = constant_buffer_->GetGPUVirtualAddress();
                cbv_desc.SizeInBytes = constant_buffer_size;
                CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle(srv_heap_->GetCPUDescriptorHandleForHeapStart(), 1,
                    static_cast<INT>(srv_descriptor_size_));
                device_->CreateConstantBufferView(&cbv_desc, desc_handle);

                // Map and initialize the constant buffer. We don't unmap this until the
                // app closes. Keeping things mapped for the lifetime of the resource is okay.
                CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
                throw_if_failed(constant_buffer_->Map(0, &readRange, reinterpret_cast<void **>(&p_cbv_data_begin_)));
                memcpy(p_cbv_data_begin_, &constant_buffer_data_, sizeof(constant_buffer_data_));
            }

            // Close the command list and execute it to begin the initial GPU setup.
            throw_if_failed(command_list_->Close(), "Closing command list in assets after texture");
            ID3D12CommandList *ppCommandLists[] = {command_list_.Get()};
            command_queue_->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Create synchronization objects.
            {
                throw_if_failed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                    "Creating fence in assets");
                fence_value_ = 1;

                // Create an event handle to use for frame synchronization.
                fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (fence_event_ == nullptr) {
                    throw_if_failed(HRESULT_FROM_WIN32(GetLastError()));
                }

                // Wait for the command list to execute; we are reusing the same command
                // list in our main loop but for now, we just want to wait for setup to
                // complete before continuing.
                wait_for_previous_frame();
            }
        }

        auto populate_command_list() const -> void
        {
            // Command list allocators can only be reset when the associated
            // command lists have finished execution on the GPU; apps should use
            // fences to determine GPU execution progress.
            throw_if_failed(command_allocator_->Reset(), "Resetting command allocator");

            // However, when ExecuteCommandList() is called on a particular command
            // list, that command list can then be reset at any time and must be before
            // re-recording.
            throw_if_failed(command_list_->Reset(command_allocator_.Get(), pipeline_state_.Get()),
                "Resetting command list");

            // Set necessary state.
            command_list_->SetGraphicsRootSignature(root_signature_.Get());

            // TODO: The descriptor heap stuff seems to be required in order for the shader object to have access to the texture data uploaded previously
            // TODO: Can this stuff be moved up to inside the bundle rather than set here each time?
            ID3D12DescriptorHeap *pp_heaps[] = {srv_heap_.Get()};
            command_list_->SetDescriptorHeaps(_countof(pp_heaps), pp_heaps);

            command_list_->SetGraphicsRootDescriptorTable(0, srv_heap_->GetGPUDescriptorHandleForHeapStart());
            const CD3DX12_GPU_DESCRIPTOR_HANDLE hdl(srv_heap_->GetGPUDescriptorHandleForHeapStart(), srv_descriptor_size_);
            command_list_->SetGraphicsRootDescriptorTable(1, hdl);
            command_list_->RSSetViewports(1, &viewport_);
            command_list_->RSSetScissorRects(1, &scissorRect_);

            // Indicate that the back buffer will be used as a render target.
            {
                const auto barriers = CD3DX12_RESOURCE_BARRIER::Transition(
                    render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                command_list_->ResourceBarrier(1, &barriers);
            }

            const CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap_->GetCPUDescriptorHandleForHeapStart(), frame_index_,
                                                          rtv_descriptor_size_);
            command_list_->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

            // Record commands.
            constexpr float clear_color[] = {0.0f, 0.2f, 0.4f, 1.0f};
            command_list_->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

            // Execute the commands stored in the bundle.
            command_list_->ExecuteBundle(bundle_command_list_.Get());

            //        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            //        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
            //        m_commandList->DrawInstanced(3, 1, 0, 0);

            // Indicate that the back buffer will now be used to present.
            {
                const auto barriers = CD3DX12_RESOURCE_BARRIER::Transition(
                    render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT);
                command_list_->ResourceBarrier(1, &barriers);
            }

            throw_if_failed(command_list_->Close(), "Closing command list");
        }

        auto wait_for_previous_frame() -> void
        {
            // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
            // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
            // sample illustrates how to use fences for efficient resource usage and to
            // maximize GPU utilization.

            // Signal and increment the fence value.
            const UINT64 fence = fence_value_;
            throw_if_failed(command_queue_->Signal(fence_.Get(), fence));
            fence_value_++;

            // Wait until the previous frame is finished.
            if (fence_->GetCompletedValue() < fence) {
                throw_if_failed(fence_->SetEventOnCompletion(fence, fence_event_));
                WaitForSingleObject(fence_event_, INFINITE);
            }

            frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        }

        static std::vector<uint8_t> generate_texture_data() {
            printf("...Generating texture data...\n");
            constexpr UINT row_pitch = texture_width * texture_pixel_size;
            constexpr UINT cell_pitch = row_pitch >> 3; // The width of a cell in the checkerboard texture.
            constexpr UINT cell_height = texture_width >> 3; // The height of a cell in the checkerboard texture.
            constexpr UINT texture_size = row_pitch * texture_height;

            std::vector<UINT8> data(texture_size);
            UINT8 *p_data = &data[0];

            for (UINT n = 0; n < texture_size; n += texture_pixel_size) {
	            const UINT x = n % row_pitch;
	            const UINT y = n / row_pitch;
	            const UINT i = x / cell_pitch;
	            const UINT j = y / cell_height;

                if (i % 2 == j % 2) {
                    p_data[n] = 0x00; // R
                    p_data[n + 1] = 0x00; // G
                    p_data[n + 2] = 0x00; // B
                    p_data[n + 3] = 0xff; // A
                } else {
                    p_data[n] = 0xff; // R
                    p_data[n + 1] = 0xff; // G
                    p_data[n + 2] = 0xff; // B
                    p_data[n + 3] = 0xff; // A
                }
            }

            return data;
        }

        static auto get_hardware_adapter(
	        _In_ IDXGIFactory1* p_factory,
	        _Outptr_result_maybenull_ IDXGIAdapter1** pp_adapter,
	        const bool request_high_performance_adapter = false) -> void
        {
            *pp_adapter = nullptr;

            ComPtr<IDXGIAdapter1> adapter;

            // Try to get an adapter using GPU preference first, which requires an IDXGIFactory6 instance
            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(p_factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
                for (
                    size_t adapter_index = 0;
                    SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                        adapter_index,
                        request_high_performance_adapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE :
                        DXGI_GPU_PREFERENCE_UNSPECIFIED,
                        IID_PPV_ARGS(&adapter)));
                    ++adapter_index) {
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
                for (UINT adapter_index = 0; SUCCEEDED(p_factory->EnumAdapters1(adapter_index, &adapter)); ++
                     adapter_index) {
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

            *pp_adapter = adapter.Detach();
        }

        std::wstring get_asset_full_path(const std::string &asset_name) const
        {
            auto asset_path = assets_path_;
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

        D3D12_VIEWPORT viewport_{};
        D3D12_RECT scissorRect_{};
        ComPtr<IDXGISwapChain3> swap_chain_;
        ComPtr<ID3D12Device> device_;
        ComPtr<ID3D12Resource> render_targets_[frame_count];
        ComPtr<ID3D12CommandAllocator> command_allocator_;
        ComPtr<ID3D12CommandAllocator> bundle_command_allocator_;
        ComPtr<ID3D12CommandQueue> command_queue_;
        ComPtr<ID3D12RootSignature> root_signature_;
        ComPtr<ID3D12DescriptorHeap> rtv_heap_; // render target view
        ComPtr<ID3D12DescriptorHeap> srv_heap_; // shader resource view
        ComPtr<ID3D12PipelineState> pipeline_state_;
        ComPtr<ID3D12GraphicsCommandList> command_list_;
        ComPtr<ID3D12GraphicsCommandList> bundle_command_list_;
        UINT rtv_descriptor_size_{};
        UINT srv_descriptor_size_{};

        // scribble_app resources.

        ComPtr<ID3D12Resource> vertex_buffer_;
        D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_{};
        ComPtr<ID3D12Resource> texture_;
        ComPtr<ID3D12Resource> constant_buffer_;
        SceneConstantBuffer constant_buffer_data_{};
        UINT8 *p_cbv_data_begin_{}; // TODO: pointers?! Yikes, don't do this...
        std::filesystem::path assets_path_;

        // Texture info

        static constexpr UINT texture_width = 256;
        static constexpr UINT texture_height = 256;
        static constexpr UINT texture_pixel_size = 4; // The number of bytes used to represent a pixel in the texture.

        // Synchronization objects.

        UINT frame_index_{};
        HANDLE fence_event_{};
        ComPtr<ID3D12Fence> fence_;
        UINT64 fence_value_{};

        // Window info/geometry

        float aspect_ratio_{};
        int width_{};
        int height_{};
        GLFWwindow *p_window_{};
    };

    export std::unique_ptr<graphics_interface> make_graphics_api() {
        return std::make_unique<dx12_interface>();
    }
} // scribble

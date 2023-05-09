cmake_minimum_required(VERSION 3.25)
project(ScribbleKit)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)

FetchContent_Declare(
    DirectX-Headers
    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
    GIT_TAG 34c98664e565a62d7c1ce50489640561c797e0c1
)
FetchContent_Declare(
    GLFW
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3fa2360720eeba1964df3c0ecf4b5df8648a8e52
)
FetchContent_MakeAvailable(DirectX-Headers)
FetchContent_MakeAvailable(GLFW)

add_executable(ScribbleKit
    main.cpp
    DX12/InterfaceDX12.cpp
    DX12/InterfaceDX12.h
    GraphicsApiInterface.h
    PlatformIncludes.h
)

target_link_libraries(ScribbleKit DirectX-Headers d3dcompiler d3d12 dxgi dxguid gdi32 glfw)
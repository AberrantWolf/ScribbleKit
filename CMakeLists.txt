cmake_minimum_required(VERSION 3.26)
project(ScribbleKit)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#------------------------------------------------
# DirectX
FetchContent_Declare(
    DirectX-Headers
    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
    GIT_TAG 34c98664e565a62d7c1ce50489640561c797e0c1
)

#------------------------------------------------
# GLFW
FetchContent_Declare(
    GLFW
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3fa2360720eeba1964df3c0ecf4b5df8648a8e52
)

#------------------------------------------------
# MathLib
FetchContent_Declare(
    MathLib
    GIT_REPOSITORY https://github.com/NVIDIAGameWorks/MathLib
    GIT_TAG 099f0ee
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_GetProperties(MathLib)
if (NOT MathLib_POPULATED)
    FetchContent_Populate(MathLib)
endif ()

#------------------------------------------------
# Make available if needed
FetchContent_MakeAvailable(DirectX-Headers GLFW)

#------------------------------------------------
# Workaround because MathLib is header-only
add_library(MathLib INTERFACE)
target_include_directories(MathLib INTERFACE ${mathlib_SOURCE_DIR})

#------------------------------------------------
# Scribble Kit!!!
add_executable(ScribbleKit
    main.cppm
)

# C++ modules are relegated to here, where they goddamn belong
target_sources(ScribbleKit
    PUBLIC
    FILE_SET all_modules TYPE CXX_MODULES FILES
    app/App.ixx
    utils/utils.ixx
    utils/utils-result.ixx
    graphics/types.ixx
    GraphicsApiInterface.ixx
    DX12/InterfaceDX12.ixx
)

# Make sure to link everything, too
target_link_libraries(ScribbleKit DirectX-Headers MathLib d3dcompiler d3d12 dxgi dxguid gdi32 glfw)

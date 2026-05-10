cmake_minimum_required(VERSION 3.15)
project(D3D12SponzaRenderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libs
)

set(SOURCES
    main.cpp
    D3D12App.cpp
    Camera/Camera.cpp
    Model/ModelLoader.cpp
    Texture/TextureLoader.cpp
    Rendering/GBuffer.cpp
    Rendering/RenderingSystem.cpp
    Rendering/Light.cpp
)

add_executable(D3D12SponzaRenderer WIN32 ${SOURCES})

if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /Ob2 /EHsc")
    
    target_compile_definitions(D3D12SponzaRenderer PRIVATE 
        WIN32_LEAN_AND_MEAN 
        NOMINMAX 
        _CRT_SECURE_NO_WARNINGS
        _ALLOW_RTCc_IN_STL
    )
endif()

target_link_libraries(D3D12SponzaRenderer
    d3d12
    dxgi
    d3dcompiler
    user32
    shell32
)

add_custom_command(TARGET D3D12SponzaRenderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:D3D12SponzaRenderer>/assets
)
cmake_minimum_required(VERSION 3.21)

project(Space
    VERSION 0.1.0
    DESCRIPTION "Space simulation 3D game"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies (provided by vcpkg via the toolchain file, see CMakePresets.json)
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)

function(space_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Geometry, model I/O and procedural generation (no OpenGL dependency)
add_library(space_geometry STATIC
    src/geometry/MeshBuilder.cpp
    src/geometry/ObjLoader.cpp
    src/geometry/ObjWriter.cpp
    src/procgen/Greebles.cpp
    src/procgen/ShipGenerator.cpp
    src/procgen/ShipMaterials.cpp
    src/procgen/ShipParts.cpp
    src/procgen/ships/LogisticsFreighter.cpp
    src/procgen/ships/WheelStation.cpp
)
target_include_directories(space_geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(space_geometry PUBLIC glm::glm)
space_warnings(space_geometry)

# OpenGL rendering and windowing
add_library(space_render STATIC
    src/core/Window.cpp
    src/render/Mesh.cpp
    src/render/Model.cpp
    src/render/Shader.cpp
    src/render/Texture.cpp
    src/render/TextureCache.cpp
    src/render/stb_image_impl.cpp
)
target_include_directories(space_render SYSTEM PUBLIC ${Stb_INCLUDE_DIR})
target_link_libraries(space_render PUBLIC space_geometry OpenGL::GL glfw GLEW::GLEW)
space_warnings(space_render)

# Game / viewer
add_executable(space src/main.cpp)
target_link_libraries(space PRIVATE space_render)
space_warnings(space)

# Command line ship generator: writes .obj/.mtl
add_executable(shipgen src/tools/shipgen.cpp)
target_link_libraries(shipgen PRIVATE space_geometry)
space_warnings(shipgen)

if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT space)
    set_property(TARGET space PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:space>")
endif()

# Copy runtime assets (shaders, textures, models) next to the executables
add_custom_command(TARGET space POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:space>/assets
    COMMENT "Copying assets"
)

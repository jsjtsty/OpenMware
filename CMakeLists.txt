cmake_minimum_required(VERSION 3.20)
project(OpenMware VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(zstd CONFIG REQUIRED)

# Squirrel 2.2.4 fork (32/64-bit bytecode ABI selectable at runtime via sq_open_ex).
add_subdirectory(third_party/squirrel2 EXCLUDE_FROM_ALL)

add_library(omw_archive STATIC
	src/archive/aes256.cpp
	src/archive/npk.cpp
)
target_include_directories(omw_archive PUBLIC src)
target_link_libraries(omw_archive PUBLIC
	$<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,zstd::libzstd_static>)

add_executable(npktool tools/npktool.cpp)
target_link_libraries(npktool PRIVATE omw_archive)

# --- Rendering (SDL3 GPU) -----------------------------------------------------------------------
find_package(SDL3 CONFIG REQUIRED)
include(cmake/OmwShaders.cmake)

add_library(omw_render STATIC src/render/gpu.cpp)
target_include_directories(omw_render PUBLIC src)
target_link_libraries(omw_render PUBLIC SDL3::SDL3)

omw_add_shader(NAME vssprite.vert STAGE vertex SOURCE ${PROJECT_SOURCE_DIR}/shaders/hlsl/sprite_vs.hlsl DEFINES VERTEX_SPRITE)
omw_add_shader(NAME pssprite_blend_normal.frag STAGE fragment SOURCE ${PROJECT_SOURCE_DIR}/shaders/hlsl/sprite.hlsl DEFINES BLEND_MODE_BLEND)
add_custom_target(omw_shaders DEPENDS ${_omw_all_shader_outputs})

add_executable(gpu_demo apps/gpu_demo.cpp)
target_link_libraries(gpu_demo PRIVATE omw_render)
add_dependencies(gpu_demo omw_shaders)

# --- Script runtime -----------------------------------------------------------------------------
add_library(omw_script STATIC src/script/host.cpp src/script/natives.cpp src/script/natives_action.cpp src/script/natives_globals.cpp src/script/natives_app.cpp)
target_include_directories(omw_script PUBLIC src)
target_link_libraries(omw_script PUBLIC omw_archive squirrel2::squirrel2)

add_executable(scriptrun apps/scriptrun.cpp)
target_link_libraries(scriptrun PRIVATE omw_script)

# Developer tool: script disassembler (uses Squirrel internals).
add_executable(nutdis tools/nutdis.cpp)
target_include_directories(nutdis PRIVATE third_party/squirrel2/squirrel)
target_link_libraries(nutdis PRIVATE squirrel2::squirrel2)

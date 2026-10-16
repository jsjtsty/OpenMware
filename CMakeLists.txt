cmake_minimum_required(VERSION 3.20)
project(OpenMware VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(zstd CONFIG REQUIRED)

# Squirrel 2.2.4 fork (32/64-bit bytecode ABI selectable at runtime via sq_open_ex).
add_subdirectory(third_party/squirrel2 EXCLUDE_FROM_ALL)

if(MSVC)
	add_compile_options(/utf-8)
endif()

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

find_package(WebP CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

add_library(omw_game STATIC src/script/easing.cpp src/game/anim.cpp src/game/text_markup.cpp src/game/stage.cpp src/game/game_files.cpp)
target_include_directories(omw_game PUBLIC src)
target_link_libraries(omw_game PUBLIC omw_archive SDL3::SDL3)

add_library(omw_render STATIC src/render/gpu.cpp src/render/renderer2d.cpp src/render/image.cpp src/render/text_raster.cpp)
target_include_directories(omw_render PUBLIC src PRIVATE ${STB_INCLUDE_DIRS})
target_link_libraries(omw_render PUBLIC omw_game SDL3::SDL3 PRIVATE WebP::webp)

# Every 2D shader of the manifest (the 3D scene shaders are not used by the game scripts).
file(STRINGS ${PROJECT_SOURCE_DIR}/shaders/hlsl/manifest.tsv _omw_shader_rows)
list(POP_FRONT _omw_shader_rows) # header
foreach(_row IN LISTS _omw_shader_rows)
	if(_row MATCHES "^([^	]+)	([^	]+)	(.*)$")
		set(_name "${CMAKE_MATCH_1}")
		set(_file "${CMAKE_MATCH_2}")
		set(_defs "${CMAKE_MATCH_3}")
		if(_name MATCHES "^scene/" OR _file MATCHES "^scene/" OR _name MATCHES "^gs")
			continue()
		endif()
		string(REGEX REPLACE "^.*/" "" _base "${_name}")
		if(_base MATCHES "^vs")
			set(_stage vertex)
			set(_ext vert)
		else()
			set(_stage fragment)
			set(_ext frag)
		endif()
		omw_add_shader(NAME ${_base}.${_ext} STAGE ${_stage} SOURCE ${PROJECT_SOURCE_DIR}/shaders/hlsl/${_file} DEFINES ${_defs})
	endif()
endforeach()
add_custom_target(omw_shaders DEPENDS ${_omw_all_shader_outputs})

add_executable(gpu_demo apps/gpu_demo.cpp)
target_link_libraries(gpu_demo PRIVATE omw_render)
add_dependencies(gpu_demo omw_shaders)

# --- Script runtime -----------------------------------------------------------------------------
add_library(omw_script STATIC src/script/host.cpp src/script/natives.cpp src/script/natives_action.cpp
	src/script/natives_globals.cpp src/script/natives_app.cpp src/script/natives_sprite.cpp src/script/natives_file.cpp src/script/natives_text.cpp)
target_include_directories(omw_script PUBLIC src)
target_link_libraries(omw_script PUBLIC omw_archive omw_game omw_render squirrel2::squirrel2)

add_executable(scriptrun apps/scriptrun.cpp)
target_link_libraries(scriptrun PRIVATE omw_script)

# Developer tool: script disassembler (uses Squirrel internals).
add_executable(nutdis tools/nutdis.cpp)
target_include_directories(nutdis PRIVATE third_party/squirrel2/squirrel)
target_link_libraries(nutdis PRIVATE squirrel2::squirrel2)

# --- The game ------------------------------------------------------------------------------------
add_executable(omw apps/omw.cpp)
target_link_libraries(omw PRIVATE omw_script omw_render omw_game)
add_dependencies(omw omw_shaders)

# Standalone SQAction1D tempo-curve checks; does not link the script runtime.
add_executable(test_easing tests/test_easing.cpp src/script/easing.cpp)
target_include_directories(test_easing PRIVATE src)
if(MSVC)
	target_compile_options(test_easing PRIVATE /fp:strict)
else()
	target_compile_options(test_easing PRIVATE -ffp-contract=off)
endif()

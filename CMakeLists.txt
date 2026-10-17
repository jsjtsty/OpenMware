cmake_minimum_required(VERSION 3.20)
project(OpenMware VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Development tools and checks (npktool, nutdis, scriptrun, gpu_demo, tests); the mobile packages only need the game itself.
if(ANDROID OR IOS)
	set(_omw_tools_default OFF)
else()
	set(_omw_tools_default ON)
endif()
option(OMW_BUILD_TOOLS "Build the developer tools" ${_omw_tools_default})

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(cmake/Dependencies.cmake) # SDL3, zstd, libwebp, stb, FFmpeg: fetched as source and built here

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
target_link_libraries(omw_archive PUBLIC zstd::libzstd_static)
target_include_directories(omw_archive PUBLIC "${zstd_SOURCE_DIR}/lib")

if(OMW_BUILD_TOOLS)
	add_executable(npktool tools/npktool.cpp)
	target_link_libraries(npktool PRIVATE omw_archive)
endif()

# --- Rendering (SDL3 GPU) -----------------------------------------------------------------------
include(cmake/OmwShaders.cmake)

add_library(omw_game STATIC src/script/easing.cpp src/game/anim.cpp src/game/text_markup.cpp src/game/input.cpp src/game/stage.cpp src/game/game_files.cpp)
target_include_directories(omw_game PUBLIC src)
target_link_libraries(omw_game PUBLIC omw_archive SDL3::SDL3)

add_library(omw_audio STATIC src/audio/audio.cpp src/audio/vorbis_impl.cpp)
target_include_directories(omw_audio PUBLIC src PRIVATE ${STB_INCLUDE_DIRS})
target_link_libraries(omw_audio PUBLIC omw_game SDL3::SDL3)

add_library(omw_video STATIC src/video/movie.cpp)
target_include_directories(omw_video PUBLIC src)
target_link_libraries(omw_video PUBLIC omw_audio omw_ffmpeg)

add_library(omw_render STATIC src/render/gpu.cpp src/render/renderer2d.cpp src/render/image.cpp src/render/text_raster.cpp)
target_include_directories(omw_render PUBLIC src PRIVATE ${STB_INCLUDE_DIRS})
target_include_directories(omw_render PRIVATE "${OMW_WEBP_INCLUDE_DIR}")
target_link_libraries(omw_render PUBLIC omw_game SDL3::SDL3 PRIVATE WebP::webpdecoder)

if(OMW_BUILD_TOOLS)
	add_executable(gpu_demo apps/gpu_demo.cpp)
	target_link_libraries(gpu_demo PRIVATE omw_render)
	add_dependencies(gpu_demo omw_shaders)
endif()

# --- Script runtime -----------------------------------------------------------------------------
add_library(omw_script STATIC src/script/host.cpp src/script/natives.cpp src/script/natives_action.cpp
	src/script/natives_globals.cpp src/script/natives_app.cpp src/script/natives_sprite.cpp src/script/natives_file.cpp src/script/natives_text.cpp src/script/natives_sound.cpp src/script/natives_movie.cpp src/script/vm_snapshot.cpp)
target_include_directories(omw_script PUBLIC src)
target_link_libraries(omw_script PUBLIC omw_archive omw_game omw_render omw_audio omw_video squirrel2::squirrel2)
target_include_directories(omw_script PRIVATE third_party/squirrel2/squirrel) # vm_snapshot.cpp walks the VM's internals

if(OMW_BUILD_TOOLS)
	add_executable(scriptrun apps/scriptrun.cpp)
	target_link_libraries(scriptrun PRIVATE omw_script)

	# Developer tool: script disassembler (uses Squirrel internals).
	add_executable(nutdis tools/nutdis.cpp)
	target_include_directories(nutdis PRIVATE third_party/squirrel2/squirrel)
	target_link_libraries(nutdis PRIVATE squirrel2::squirrel2)
endif()

# --- The game ------------------------------------------------------------------------------------
# One run of the game (window, renderer, audio, script loop), shared by the command-line program and the library.
add_library(omw_session STATIC src/api/session.cpp)
target_link_libraries(omw_session PUBLIC omw_script omw_render omw_game)
add_dependencies(omw_session omw_shaders)

# FFmpeg (LGPL) is a set of shared libraries that ship next to whatever links them (see omw_ffmpeg_runtime below).
# Android: SDL's Java activity loads libmain.so and calls SDL_main in it. iOS has no command-line program, only libmware.
if(ANDROID)
	add_library(openmware_player SHARED apps/openmware.cpp)
	set_target_properties(openmware_player PROPERTIES OUTPUT_NAME main)
elseif(NOT IOS)
	add_executable(openmware_player apps/openmware.cpp)
	# The command-line player is openmware-player everywhere; the launcher apps (launcher/) are the programs named OpenMware.
	set_target_properties(openmware_player PROPERTIES OUTPUT_NAME openmware-player)
endif()
if(TARGET openmware_player)
	target_link_libraries(openmware_player PRIVATE omw_session)
endif()

# libmware: the engine as a library for the SwiftUI launcher (iOS in-process player, macOS probe), a framework on Apple.
option(OMW_BUILD_LIBRARY "Build libmware (include/mware/mware.h)" ${APPLE})
if(OMW_BUILD_LIBRARY)
	add_library(mware SHARED src/api/mware.cpp)
	target_include_directories(mware PUBLIC include)
	target_link_libraries(mware PRIVATE omw_session)
	if(APPLE)
		set_target_properties(mware PROPERTIES
			FRAMEWORK TRUE
			FRAMEWORK_VERSION A
			PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/mware/mware.h"
			MACOSX_FRAMEWORK_IDENTIFIER org.openmware.mware
			MACOSX_FRAMEWORK_BUNDLE_VERSION "${PROJECT_VERSION}"
			MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${PROJECT_VERSION}"
			XCODE_ATTRIBUTE_INSTALL_PATH "@rpath")
		# The launcher puts the FFmpeg dylibs next to mware.framework, in its Frameworks folder.
		set_target_properties(mware PROPERTIES
			BUILD_RPATH "@loader_path/..;@loader_path/../../.." INSTALL_RPATH "@loader_path/..;@loader_path/../../..")
	endif()
endif()

# The Qt launcher for Windows and Linux (Qt 6 Widgets, LGPL as shared libraries); starts openmware-player next to it.
option(OMW_BUILD_LAUNCHER "Build the Qt launcher (Windows, Linux)" OFF)
if(OMW_BUILD_LAUNCHER)
	add_subdirectory(launcher/qt)
endif()

function(omw_ffmpeg_runtime target)
	if(NOT (TARGET ffmpeg_ext AND OMW_FFMPEG_SHARED))
		return()
	endif()
	if(WIN32)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${OMW_FFMPEG_RUNTIME_DIR}" $<TARGET_FILE_DIR:${target}>)
	elseif(APPLE)
		set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
	elseif(NOT ANDROID)
		set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${OMW_FFMPEG_RUNTIME_DIR}" $<TARGET_FILE_DIR:${target}>/lib)
	endif()
endfunction()
if(TARGET openmware_player)
	omw_ffmpeg_runtime(openmware_player)
	if(OMW_FFMPEG_DLLS) # only with OMW_FFMPEG_ROOT pointing at a shared FFmpeg
		add_custom_command(TARGET openmware_player POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OMW_FFMPEG_DLLS} $<TARGET_FILE_DIR:openmware_player>)
	endif()
endif()

# Standalone SQAction1D tempo-curve checks; does not link the script runtime.
if(OMW_BUILD_TOOLS)
	add_executable(test_easing tests/test_easing.cpp src/script/easing.cpp)
	target_include_directories(test_easing PRIVATE src)
	if(MSVC)
		target_compile_options(test_easing PRIVATE /fp:strict)
	else()
		target_compile_options(test_easing PRIVATE -ffp-contract=off)
	endif()
endif()

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
# Android: SDL's Java activity loads libmain.so and calls SDL_main in it. Everywhere else it is the executable "omw"
# (an app bundle on macOS and iOS, with the shaders inside).
if(ANDROID)
	add_library(omw SHARED apps/omw.cpp)
	set_target_properties(omw PROPERTIES OUTPUT_NAME main)
else()
	add_executable(omw apps/omw.cpp)
endif()
target_link_libraries(omw PRIVATE omw_script omw_render omw_game)
add_dependencies(omw omw_shaders)
if(OMW_FFMPEG_DLLS) # only with OMW_FFMPEG_ROOT pointing at a shared FFmpeg
	add_custom_command(TARGET omw POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OMW_FFMPEG_DLLS} $<TARGET_FILE_DIR:omw>)
endif()

if(APPLE)
	set_target_properties(omw PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_GUI_IDENTIFIER "org.openmware.omw"
		MACOSX_BUNDLE_BUNDLE_NAME "OpenMware"
		MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
		MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/Info.plist.in"
		XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.openmware.omw")
	# The engine finds its shaders next to the bundle's resources (SDL_GetBasePath).
	if(IOS)
		set(_omw_shader_dest "$<TARGET_BUNDLE_CONTENT_DIR:omw>/shaders")
	else()
		set(_omw_shader_dest "$<TARGET_BUNDLE_CONTENT_DIR:omw>/Resources/shaders")
	endif()
	add_custom_command(TARGET omw POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${OMW_SHADER_OUT}" "${_omw_shader_dest}"
		COMMENT "Copying shaders into the app bundle")
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

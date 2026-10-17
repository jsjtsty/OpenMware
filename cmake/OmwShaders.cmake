# The 2D shaders are compiled ahead of time (cmake/OmwShaderGen.cmake, see docs/building.md) into the three formats SDL_GPU
# backends take, and the results are checked in under shaders/prebuilt/<format>:
#   spv   SPIR-V    Vulkan   (Windows, Linux, Android)
#   dxil  DXIL      Direct3D 12 (Windows)
#   msl   MSL       Metal    (macOS, iOS)
# A build only copies the sets its platform needs to <build>/shaders (the engine looks for them next to the executable;
# Apple bundles and the Android APK get them packaged from there / from the source tree).
#
# OMW_SHADER_FORMATS overrides the choice: a list of spv / dxil / msl, or "all".
set(OMW_SHADER_FORMATS "" CACHE STRING "Shader formats to ship: spv;dxil;msl or 'all' (default: by platform)")
option(OMW_REGEN_SHADERS "Recompile the HLSL sources with SDL_shadercross into shaders/prebuilt (developers only)" OFF)

if(NOT OMW_SHADER_FORMATS)
	if(WIN32)
		set(_omw_formats spv dxil)
	elseif(APPLE)
		set(_omw_formats msl)
	else()
		set(_omw_formats spv)
	endif()
elseif(OMW_SHADER_FORMATS STREQUAL "all")
	set(_omw_formats spv dxil msl)
else()
	set(_omw_formats ${OMW_SHADER_FORMATS})
endif()

set(OMW_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
set(OMW_PREBUILT_DIR "${PROJECT_SOURCE_DIR}/shaders/prebuilt")

if(OMW_REGEN_SHADERS)
	include(cmake/OmwShaderGen.cmake)
endif()

add_custom_target(omw_shaders COMMENT "Copying shaders (${_omw_formats})")
foreach(fmt IN LISTS _omw_formats)
	if(NOT IS_DIRECTORY "${OMW_PREBUILT_DIR}/${fmt}")
		message(FATAL_ERROR "No prebuilt ${fmt} shaders in ${OMW_PREBUILT_DIR}/${fmt}; run a build with -DOMW_REGEN_SHADERS=ON")
	endif()
	add_custom_command(TARGET omw_shaders POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${OMW_PREBUILT_DIR}/${fmt}" "${OMW_SHADER_OUT}")
endforeach()
if(OMW_REGEN_SHADERS)
	add_dependencies(omw_shaders omw_shaders_regen)
endif()

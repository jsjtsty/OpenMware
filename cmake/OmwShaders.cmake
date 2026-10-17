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

# Metal: the MSL files are source. On Apple hosts with the Metal compiler they are compiled to .metallib libraries here
# (per SDK, so the committed set stays platform independent); without the compiler the MSL source is shipped and the
# driver compiles it at load time.
set(_omw_metal_sdk "")
if(APPLE AND "msl" IN_LIST _omw_formats)
	if(IOS)
		set(_omw_metal_sdk iphoneos)
		set(_omw_metal_min "-mios-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
	else()
		set(_omw_metal_sdk macosx)
		set(_omw_metal_min "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
		if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
			set(_omw_metal_min "")
		endif()
	endif()
	execute_process(COMMAND xcrun --sdk ${_omw_metal_sdk} --find metal RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
	execute_process(COMMAND xcrun --sdk ${_omw_metal_sdk} --find metallib RESULT_VARIABLE _rc2 OUTPUT_QUIET ERROR_QUIET)
	if(_rc OR _rc2)
		message(WARNING "Metal compiler not found (xcodebuild -downloadComponent MetalToolchain): shipping MSL source")
		set(_omw_metal_sdk "")
	endif()
endif()

add_custom_target(omw_shaders COMMENT "Preparing shaders (${_omw_formats})")
foreach(fmt IN LISTS _omw_formats)
	if(NOT IS_DIRECTORY "${OMW_PREBUILT_DIR}/${fmt}")
		message(FATAL_ERROR "No prebuilt ${fmt} shaders in ${OMW_PREBUILT_DIR}/${fmt}; run a build with -DOMW_REGEN_SHADERS=ON")
	endif()
	if(fmt STREQUAL msl AND _omw_metal_sdk)
		add_custom_command(TARGET omw_shaders POST_BUILD
			COMMAND ${CMAKE_COMMAND} -DSDK=${_omw_metal_sdk} "-DMIN=${_omw_metal_min}" -DIN=${OMW_PREBUILT_DIR}/msl
				-DOUT=${OMW_SHADER_OUT} -DTMP=${CMAKE_BINARY_DIR}/metal_tmp -P ${PROJECT_SOURCE_DIR}/cmake/CompileMetal.cmake)
	else()
		add_custom_command(TARGET omw_shaders POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${OMW_PREBUILT_DIR}/${fmt}" "${OMW_SHADER_OUT}")
	endif()
endforeach()
if(OMW_REGEN_SHADERS)
	add_dependencies(omw_shaders omw_shaders_regen)
endif()

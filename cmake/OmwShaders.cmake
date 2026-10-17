# omw_add_shader(NAME <out-name> STAGE vertex|fragment SOURCE <hlsl> [DEFINES A B ...])
# Produces <bindir>/shaders/<NAME>.{spv,dxil,msl} via SDL_shadercross, after patching registers to SDL_gpu spaces.
#
# Which formats are built (OMW_SHADER_FORMATS, a list of spv / dxil / msl): the ones the platform's GPU backends take
# (Windows: SPIR-V for Vulkan + DXIL for D3D12, Linux and Android: SPIR-V, Apple: MSL). "all" builds the three of them,
# which is what the CI shader job does to hand the result to hosts that cannot run shadercross (macOS, Android, iOS:
# its DXC dependency only exists for Windows and Linux x64). On those hosts pass OMW_PREBUILT_SHADERS=<dir> instead.
set(OMW_PREBUILT_SHADERS "" CACHE PATH "Directory of ready-made shader binaries (skips shadercross)")
set(OMW_SHADER_FORMATS "" CACHE STRING "Shader formats to build: spv;dxil;msl or 'all' (default: by platform)")

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
set(_omw_all_shader_outputs "")

if(OMW_PREBUILT_SHADERS)
	# Nothing to compile: omw_shaders copies the ready-made files to where the build expects them.
	add_custom_target(omw_shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${OMW_PREBUILT_SHADERS}" "${OMW_SHADER_OUT}"
		COMMENT "Using prebuilt shaders from ${OMW_PREBUILT_SHADERS}")
	function(omw_add_shader)
	endfunction()
	return()
endif()

find_program(OMW_SHADERCROSS shadercross
	HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/sdl3-shadercross"
	      "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/bin"
	      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sdl3-shadercross"
	      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
	DOC "SDL_shadercross command line tool")
if(NOT OMW_SHADERCROSS)
	message(FATAL_ERROR "shadercross not found. Install the vcpkg feature 'shader-tools' (Windows / Linux x64) or pass "
		"-DOMW_PREBUILT_SHADERS=<dir> with shaders built on such a host.")
endif()

function(omw_add_shader)
	cmake_parse_arguments(S "" "NAME;STAGE;SOURCE" "DEFINES" ${ARGN})
	set(_patched "${CMAKE_BINARY_DIR}/shader_src/${S_NAME}.hlsl")
	get_filename_component(_src_dir "${S_SOURCE}" DIRECTORY)
	set(_defs "")
	foreach(d IN LISTS S_DEFINES)
		list(APPEND _defs "-D${d}=1")
	endforeach()
	add_custom_command(OUTPUT "${_patched}"
		COMMAND ${CMAKE_COMMAND} -DIN=${S_SOURCE} -DOUT=${_patched} -DSTAGE=${S_STAGE} -P ${PROJECT_SOURCE_DIR}/cmake/PatchHlsl.cmake
		DEPENDS "${S_SOURCE}" "${PROJECT_SOURCE_DIR}/cmake/PatchHlsl.cmake" VERBATIM)
	set(_outs "")
	foreach(fmt IN LISTS _omw_formats)
		string(TOUPPER ${fmt} _D)
		if(fmt STREQUAL spv)
			set(_D SPIRV)
		endif()
		set(_out "${OMW_SHADER_OUT}/${S_NAME}.${fmt}")
		add_custom_command(OUTPUT "${_out}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${OMW_SHADER_OUT}"
			COMMAND ${OMW_SHADERCROSS} "${_patched}" -s HLSL -d ${_D} -t ${S_STAGE} -e main -I "${_src_dir}" ${_defs} -o "${_out}"
			DEPENDS "${_patched}" VERBATIM)
		list(APPEND _outs "${_out}")
	endforeach()
	set(_omw_all_shader_outputs ${_omw_all_shader_outputs} ${_outs} PARENT_SCOPE)
endfunction()

# Developer-only: recompiles the HLSL sources (shaders/hlsl, listed in manifest.tsv) with SDL_shadercross into
# shaders/prebuilt/{spv,dxil,msl}. Needs the `shadercross` tool on PATH (or OMW_SHADERCROSS=<exe>); the result is meant
# to be committed, so ordinary builds never need shadercross (or DXC).
#
# omw_add_shader(NAME <out-name> STAGE vertex|fragment SOURCE <hlsl> [DEFINES A B ...])
# Registers <prebuilt>/<fmt>/<NAME>.<fmt> for the three formats, after patching registers to SDL_gpu spaces.
find_program(OMW_SHADERCROSS shadercross DOC "SDL_shadercross command line tool")
if(NOT OMW_SHADERCROSS)
	message(FATAL_ERROR "OMW_REGEN_SHADERS needs the shadercross tool (build SDL_shadercross, put it on PATH or set OMW_SHADERCROSS)")
endif()

set(_omw_all_shader_outputs "")

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
	foreach(fmt spv dxil msl)
		set(_D SPIRV)
		if(fmt STREQUAL dxil)
			set(_D DXIL)
		elseif(fmt STREQUAL msl)
			set(_D MSL)
		endif()
		set(_out "${OMW_PREBUILT_DIR}/${fmt}/${S_NAME}.${fmt}")
		add_custom_command(OUTPUT "${_out}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${OMW_PREBUILT_DIR}/${fmt}"
			COMMAND ${OMW_SHADERCROSS} "${_patched}" -s HLSL -d ${_D} -t ${S_STAGE} -e main -I "${_src_dir}" ${_defs} -o "${_out}"
			DEPENDS "${_patched}" VERBATIM)
		list(APPEND _outs "${_out}")
	endforeach()
	set(_omw_all_shader_outputs ${_omw_all_shader_outputs} ${_outs} PARENT_SCOPE)
endfunction()

# Every 2D shader of the manifest (the 3D scene shaders are not used by the game scripts).
# The define lists contain ';', so rows are split by hand (file(STRINGS) would break them into list elements).
file(READ ${PROJECT_SOURCE_DIR}/shaders/hlsl/manifest.tsv _omw_manifest)
string(REPLACE ";" "|" _omw_manifest "${_omw_manifest}")
string(REPLACE "\r" "" _omw_manifest "${_omw_manifest}")
string(REPLACE "\n" ";" _omw_shader_rows "${_omw_manifest}")
list(POP_FRONT _omw_shader_rows) # header
foreach(_row IN LISTS _omw_shader_rows)
	if(_row MATCHES "^([^	]+)	([^	]+)	(.*)$")
		set(_name "${CMAKE_MATCH_1}")
		set(_file "${CMAKE_MATCH_2}")
		string(REPLACE "|" ";" _defs "${CMAKE_MATCH_3}")
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
# OpenMware's own sprite effect shaders (blur, colour adjustments).
omw_add_shader(NAME psfx_blur.frag STAGE fragment SOURCE ${PROJECT_SOURCE_DIR}/shaders/hlsl/fx.hlsl DEFINES FX_BLUR)
omw_add_shader(NAME psfx_color.frag STAGE fragment SOURCE ${PROJECT_SOURCE_DIR}/shaders/hlsl/fx.hlsl DEFINES FX_COLOR)
omw_add_shader(NAME psfx_warp.frag STAGE fragment SOURCE ${PROJECT_SOURCE_DIR}/shaders/hlsl/fx.hlsl DEFINES FX_WARP)
add_custom_target(omw_shaders_regen DEPENDS ${_omw_all_shader_outputs})

# omw_add_shader(TARGET <t> NAME <out-name> STAGE vertex|fragment SOURCE <hlsl> [DEFINES A B ...])
# Produces <bindir>/shaders/<NAME>.{spv,dxil[,msl]} via SDL_shadercross, after patching registers to SDL_gpu spaces.
find_program(OMW_SHADERCROSS shadercross
	HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sdl3-shadercross"
	      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
	REQUIRED)

set(OMW_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
set(OMW_SHADER_FORMATS spv dxil)
if(APPLE)
	list(APPEND OMW_SHADER_FORMATS msl)
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
	foreach(fmt IN LISTS OMW_SHADER_FORMATS)
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

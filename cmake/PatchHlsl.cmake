# Rewrites register bindings of a plain D3D11-style HLSL file to the SDL_gpu conventions.
#   cmake -DIN=... -DOUT=... -DSTAGE=vertex|fragment -P PatchHlsl.cmake
# Vertex:   uniform buffers -> space1 (textures/samplers stay in space0)
# Fragment: textures/samplers -> space2, uniform buffers -> space3
file(READ "${IN}" _src)
if(STAGE STREQUAL "fragment")
	string(REGEX REPLACE "register\\(([ts][0-9]+)\\)" "register(\\1, space2)" _src "${_src}")
	string(REGEX REPLACE "register\\(b([0-9]+)\\)" "register(b\\1, space3)" _src "${_src}")
else()
	string(REGEX REPLACE "register\\(b([0-9]+)\\)" "register(b\\1, space1)" _src "${_src}")
endif()
file(WRITE "${OUT}" "${_src}")

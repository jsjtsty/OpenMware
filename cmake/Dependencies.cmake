# Third-party code, fetched as source and built by this project (no package manager involved), with everything the engine
# does not use switched off. Versions are pinned by URL + SHA-256. FFmpeg is separate (cmake/FFmpeg.cmake).
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Libraries are linked statically, except SDL3 on Android, where SDL's Java side loads libSDL3.so.
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # old third-party CMakeLists

# --- SDL3: window, input, audio device, GPU (Vulkan / Direct3D 12 / Metal) ----------------------
if(ANDROID)
	set(SDL_SHARED ON CACHE BOOL "" FORCE)
	set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
foreach(_opt SDL_TESTS SDL_TEST_LIBRARY SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_INSTALL_DOCS
		SDL_CAMERA SDL_SENSOR SDL_HAPTIC SDL_POWER SDL_DIALOG SDL_TRAY SDL_RENDER SDL_OFFSCREEN
		SDL_DUMMYVIDEO SDL_DUMMYAUDIO SDL_OPENGL SDL_OPENGLES SDL_KMSDRM SDL_RPI SDL_PIPEWIRE SDL_JACK SDL_SNDIO)
	set(${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
if(NOT ANDROID)
	set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE) # the game reads keyboard and mouse only
	set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
endif()
set(SDL_GPU ON CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
	URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
	URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	EXCLUDE_FROM_ALL)

# --- zstd: the archives are zstd-compressed; only decompression is used ---------------------------
foreach(_opt ZSTD_BUILD_PROGRAMS ZSTD_BUILD_CONTRIB ZSTD_BUILD_TESTS ZSTD_BUILD_SHARED ZSTD_BUILD_COMPRESSOR
		ZSTD_BUILD_DICTBUILDER ZSTD_BUILD_DEPRECATED ZSTD_LEGACY_SUPPORT ZSTD_MULTITHREAD_SUPPORT)
	set(${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_DECOMPRESSOR ON CACHE BOOL "" FORCE)
FetchContent_Declare(zstd
	URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
	URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	SOURCE_SUBDIR build/cmake
	EXCLUDE_FROM_ALL)

# --- libwebp: decoder only (the pictures are .webp) ---------------------------------------------
foreach(_opt WEBP_BUILD_ANIM_UTILS WEBP_BUILD_CWEBP WEBP_BUILD_DWEBP WEBP_BUILD_GIF2WEBP WEBP_BUILD_IMG2WEBP
		WEBP_BUILD_VWEBP WEBP_BUILD_WEBPINFO WEBP_BUILD_WEBPMUX WEBP_BUILD_EXTRAS WEBP_BUILD_LIBWEBPMUX
		WEBP_BUILD_WEBP_JS WEBP_BUILD_FUZZTEST WEBP_BUILD_TESTS)
	set(${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(libwebp
	URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.6.0.tar.gz
	URL_HASH SHA256=93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	EXCLUDE_FROM_ALL)

# --- stb: header-only (image loading, TrueType rasterising, Ogg Vorbis) ----------------------------
FetchContent_Declare(stb
	URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
	URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(SDL3 zstd libwebp stb)

set(STB_INCLUDE_DIRS "${stb_SOURCE_DIR}")
if(NOT TARGET zstd::libzstd_static)
	add_library(zstd::libzstd_static ALIAS libzstd_static)
endif()
if(NOT TARGET WebP::webpdecoder)
	add_library(WebP::webpdecoder ALIAS webpdecoder)
endif()
set(OMW_WEBP_INCLUDE_DIR "${libwebp_SOURCE_DIR}/src")

include(cmake/FFmpeg.cmake)

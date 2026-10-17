# FFmpeg, built from source with only what the cutscenes need: the ASF demuxer and the WMV3 / VC-1 video and WMA2 / WMA Pro
# audio decoders, plus swscale (to RGBA) and swresample (to S16). Result: the imported target omw_ffmpeg.
#
# OMW_FFMPEG_ROOT=<prefix> uses an existing FFmpeg (include/ and lib/ below the prefix) instead of building one: for quick
# local experiments, not for releases. Otherwise FFmpeg's configure/make runs as an ExternalProject, which needs a POSIX
# shell and make: native on Linux and macOS, the NDK / Xcode toolchains when cross compiling for Android / iOS, and MSYS2
# (C:/msys64 or OMW_MSYS2_ROOT, with make, diffutils and nasm; /usr/bin/link.exe renamed away) for MSVC on Windows.
include(ExternalProject)

set(OMW_FFMPEG_ROOT "" CACHE PATH "Use this FFmpeg prefix instead of building FFmpeg from source")
set(OMW_FFMPEG_VERSION n9.0.2)
set(_omw_ff_libs avformat avcodec swscale swresample avutil)

add_library(omw_ffmpeg INTERFACE)

if(OMW_FFMPEG_ROOT)
	target_include_directories(omw_ffmpeg INTERFACE "${OMW_FFMPEG_ROOT}/include")
	foreach(_l IN LISTS _omw_ff_libs)
		find_library(_omw_ff_${_l} NAMES ${_l} lib${_l} PATHS "${OMW_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
		target_link_libraries(omw_ffmpeg INTERFACE "${_omw_ff_${_l}}")
	endforeach()
	if(EXISTS "${OMW_FFMPEG_ROOT}/bin")
		file(GLOB OMW_FFMPEG_DLLS "${OMW_FFMPEG_ROOT}/bin/*.dll")
	endif()
	return()
endif()

set(_omw_ff_prefix "${CMAKE_BINARY_DIR}/ffmpeg-install")
set(_omw_ff_src_hint "")

# --- Target description -------------------------------------------------------------------------
set(_ff_args
	--disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-network
	--disable-autodetect --disable-debug --enable-static --disable-shared --enable-pic
	--enable-avcodec --enable-avformat --enable-swscale --enable-swresample
	--enable-demuxer=asf --enable-parser=vc1
	--enable-decoder=wmv3,vc1,wmapro,wmav2
	"--prefix=${_omw_ff_prefix}")

set(_ff_pre "")
set(_ff_system_libs m)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _ff_cpu)
if(CMAKE_OSX_ARCHITECTURES)
	set(_ff_cpu "${CMAKE_OSX_ARCHITECTURES}")
endif()
set(_ff_x86 FALSE)
if(_ff_cpu MATCHES "x86_64|amd64|^x86$|i[3-6]86")
	set(_ff_x86 TRUE)
endif()

if(ANDROID)
	string(REGEX REPLACE "/bin/.*$" "" _ndk_llvm "${CMAKE_C_COMPILER}")
	get_filename_component(_ndk_bin "${CMAKE_C_COMPILER}" DIRECTORY)
	if(ANDROID_ABI STREQUAL "arm64-v8a")
		set(_ff_arch aarch64)
		set(_ff_triple aarch64-linux-android)
	elseif(ANDROID_ABI STREQUAL "x86_64")
		set(_ff_arch x86_64)
		set(_ff_triple x86_64-linux-android)
	else()
		message(FATAL_ERROR "FFmpeg for Android ${ANDROID_ABI}: add the ABI here")
	endif()
	list(APPEND _ff_args --enable-cross-compile --target-os=android "--arch=${_ff_arch}"
		"--cc=${_ndk_bin}/${_ff_triple}${ANDROID_PLATFORM_LEVEL}-clang"
		"--ar=${_ndk_bin}/llvm-ar" "--nm=${_ndk_bin}/llvm-nm" "--ranlib=${_ndk_bin}/llvm-ranlib"
		"--strip=${_ndk_bin}/llvm-strip")
elseif(IOS)
	execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE _ios_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND xcrun --sdk iphoneos --find clang OUTPUT_VARIABLE _ios_cc OUTPUT_STRIP_TRAILING_WHITESPACE)
	set(_ios_flags "-arch arm64 -isysroot ${_ios_sdk} -miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
	list(APPEND _ff_args --enable-cross-compile --target-os=darwin --arch=arm64 "--cc=${_ios_cc}"
		"--extra-cflags=${_ios_flags}" "--extra-ldflags=${_ios_flags}")
	set(_ff_x86 FALSE)
elseif(APPLE)
	execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _mac_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
	set(_mac_flags "-arch ${_ff_cpu} -isysroot ${_mac_sdk}")
	if(CMAKE_OSX_DEPLOYMENT_TARGET)
		string(APPEND _mac_flags " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
	endif()
	list(APPEND _ff_args "--extra-cflags=${_mac_flags}" "--extra-ldflags=${_mac_flags}")
	list(APPEND _ff_system_libs "-framework CoreFoundation" "-framework Security")
elseif(WIN32 AND MSVC)
	list(APPEND _ff_args --toolchain=msvc --target-os=win64 --arch=x86_64)
	list(APPEND _ff_system_libs bcrypt ws2_32 user32 ole32)
	set(_ff_pre "export MSYS2_PATH_TYPE=inherit")
endif()

# x86 needs nasm for the fast paths; without it FFmpeg still builds, just slower.
if(_ff_x86)
	set(_msys_hint "")
	if(WIN32)
		set(_msys_hint "$ENV{OMW_MSYS2_ROOT}/usr/bin" "C:/msys64/usr/bin")
	endif()
	find_program(OMW_NASM nasm HINTS ${_msys_hint})
	if(NOT OMW_NASM)
		list(APPEND _ff_args --disable-x86asm)
		message(STATUS "FFmpeg: nasm not found, building without x86 assembly")
	endif()
endif()

# --- Shell -------------------------------------------------------------------------------------
if(WIN32)
	set(_msys_root "$ENV{OMW_MSYS2_ROOT}")
	if(NOT _msys_root)
		set(_msys_root "C:/msys64")
	endif()
	set(_omw_sh "${_msys_root}/usr/bin/bash.exe")
	if(NOT EXISTS "${_omw_sh}")
		message(FATAL_ERROR "Building FFmpeg on Windows needs MSYS2 (set OMW_MSYS2_ROOT, or OMW_FFMPEG_ROOT to use a "
			"ready-made FFmpeg). Not found: ${_omw_sh}")
	endif()
	set(_omw_sh_args -l)
	# C:/x/y -> /c/x/y for the MSYS shell
	function(omw_msys_path out in)
		if(in MATCHES "^([A-Za-z]):(.*)$")
			string(TOLOWER "${CMAKE_MATCH_1}" _d)
			set(${out} "/${_d}${CMAKE_MATCH_2}" PARENT_SCOPE)
		else()
			set(${out} "${in}" PARENT_SCOPE)
		endif()
	endfunction()
else()
	find_program(_omw_sh bash sh REQUIRED)
	set(_omw_sh_args "")
	function(omw_msys_path out in)
		set(${out} "${in}" PARENT_SCOPE)
	endfunction()
endif()

omw_msys_path(_ff_prefix_sh "${_omw_ff_prefix}")
set(_ff_quoted "")
foreach(_a IN LISTS _ff_args)
	if(WIN32 AND _a MATCHES "^--prefix=(.*)$")
		omw_msys_path(_p "${CMAKE_MATCH_1}")
		set(_a "--prefix=${_p}")
	endif()
	string(REPLACE "'" "'\\''" _a "${_a}")
	string(APPEND _ff_quoted " '${_a}'")
endforeach()

set(_ff_build_dir "${CMAKE_BINARY_DIR}/ffmpeg-build")
set(_ff_script "${CMAKE_BINARY_DIR}/ffmpeg_build.sh")
omw_msys_path(_ff_build_sh "${_ff_build_dir}")
file(WRITE "${_ff_script}"
"set -e
${_ff_pre}
cd '${_ff_build_sh}'
if [ ! -f config.h ]; then
  \"\$1/configure\"${_ff_quoted}
fi
make -j\"\$(nproc 2>/dev/null || sysctl -n hw.ncpu)\"
make install
")
omw_msys_path(_ff_script_sh "${_ff_script}")
set(_ff_src_dir "${CMAKE_BINARY_DIR}/ffmpeg-src")
omw_msys_path(_ff_src_sh "${_ff_src_dir}")

# --- Byproducts: static libraries ---------------------------------------------------------------
set(_ff_byproducts "")
foreach(_l IN LISTS _omw_ff_libs)
	if(MSVC)
		list(APPEND _ff_byproducts "${_omw_ff_prefix}/lib/${_l}.lib")
	else()
		list(APPEND _ff_byproducts "${_omw_ff_prefix}/lib/lib${_l}.a")
	endif()
endforeach()

file(MAKE_DIRECTORY "${_ff_build_dir}" "${_omw_ff_prefix}/include")

ExternalProject_Add(ffmpeg_ext
	URL https://github.com/FFmpeg/FFmpeg/archive/refs/tags/${OMW_FFMPEG_VERSION}.tar.gz
	URL_HASH SHA256=6e374ed621e48faa40639307dff48ba6fe574a509977956d2cce9669b7cc27e9
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	SOURCE_DIR "${_ff_src_dir}"
	BINARY_DIR "${_ff_build_dir}"
	CONFIGURE_COMMAND ""
	BUILD_COMMAND "${_omw_sh}" ${_omw_sh_args} "${_ff_script_sh}" "${_ff_src_sh}"
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS ${_ff_byproducts}
	USES_TERMINAL_BUILD TRUE)

target_include_directories(omw_ffmpeg INTERFACE "${_omw_ff_prefix}/include")
add_dependencies(omw_ffmpeg ffmpeg_ext)
foreach(_l IN LISTS _omw_ff_libs)
	if(MSVC)
		target_link_libraries(omw_ffmpeg INTERFACE "${_omw_ff_prefix}/lib/${_l}.lib")
	else()
		target_link_libraries(omw_ffmpeg INTERFACE "${_omw_ff_prefix}/lib/lib${_l}.a")
	endif()
endforeach()
target_link_libraries(omw_ffmpeg INTERFACE ${_ff_system_libs})

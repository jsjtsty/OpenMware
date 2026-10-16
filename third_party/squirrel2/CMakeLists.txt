cmake_minimum_required(VERSION 3.20)

project(squirrel2 VERSION 2.2.4 LANGUAGES CXX)

option(SQUIRREL2_BUILD_SHARED "Build squirrel2 as a shared library" ${BUILD_SHARED_LIBS})

set(_squirrel2_library_type STATIC)
if(SQUIRREL2_BUILD_SHARED)
	set(_squirrel2_library_type SHARED)
endif()

add_library(squirrel2 ${_squirrel2_library_type})
add_library(squirrel2::squirrel2 ALIAS squirrel2)

target_sources(squirrel2
	PRIVATE
		squirrel/sqapi.cpp
		squirrel/sqbaselib.cpp
		squirrel/sqcompiler.cpp
		squirrel/sqdebug.cpp
		squirrel/sqfuncstate.cpp
		squirrel/sqlexer.cpp
		squirrel/sqobject.cpp
		squirrel/sqstate.cpp
		squirrel/sqtable.cpp
		squirrel/sqvm.cpp
		squirrel/sqmem.cpp
		squirrel/sqclass.cpp
		sqstdlib/sqstdaux.cpp
		sqstdlib/sqstdblob.cpp
		sqstdlib/sqstdio.cpp
		sqstdlib/sqstdmath.cpp
		sqstdlib/sqstdrex.cpp
		sqstdlib/sqstdstream.cpp
		sqstdlib/sqstdstring.cpp
		sqstdlib/sqstdsystem.cpp
)

target_include_directories(squirrel2
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/squirrel
		${CMAKE_CURRENT_SOURCE_DIR}/sqstdlib
)

target_compile_features(squirrel2 PUBLIC cxx_std_20)

if(SQUIRREL2_BUILD_SHARED)
	target_compile_definitions(squirrel2 PRIVATE SQUIRREL_BUILD_SHARED)
	if(NOT WIN32)
		target_compile_definitions(squirrel2 PRIVATE SQUIRREL_USE_SHARED)
	endif()
endif()

set_target_properties(squirrel2 PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	OUTPUT_NAME squirrel2
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(SQUIRREL2_BUILD_SHARED AND WIN32)
	set_target_properties(squirrel2 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

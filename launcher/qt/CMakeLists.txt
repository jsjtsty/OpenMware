# The Qt 6 launcher for Windows and Linux: the game library, adding games, keys, saves; it starts openmware-player.
# Qt is used as shared libraries (LGPL): windeployqt copies them next to the program on Windows, Linux uses the distribution's.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON) # qt_standard_project_setup only does this from Qt 6.5 on

add_executable(openmware_launcher WIN32
	src/main.cpp
	src/Strings.cpp
	src/Library.cpp
	src/ZipWriter.cpp
	src/Player.cpp
	src/GameDelegate.cpp
	src/MainWindow.cpp
	src/AddGameDialog.cpp
	src/AboutDialog.cpp
	resources/launcher.qrc)
target_compile_features(openmware_launcher PRIVATE cxx_std_20)
target_link_libraries(openmware_launcher PRIVATE Qt6::Widgets)
target_compile_definitions(openmware_launcher PRIVATE OMW_VERSION="${PROJECT_VERSION}")
if(MSVC)
	target_compile_options(openmware_launcher PRIVATE /utf-8)
endif()
if(WIN32)
	target_sources(openmware_launcher PRIVATE resources/launcher.rc)
	set_target_properties(openmware_launcher PROPERTIES OUTPUT_NAME OpenMware)
else()
	set_target_properties(openmware_launcher PROPERTIES OUTPUT_NAME openmware)
endif()
add_dependencies(openmware_launcher openmware_player) # the launcher starts openmware-player from its own folder

# Same folder as the player and its shaders, so a build tree is runnable as it is.
set_target_properties(openmware_launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

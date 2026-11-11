# The PC launcher: profiles, the game's live log, news and setting up the game files from the player's own dumps.
# It is built with the Qt that tools/build/qt.py installs into build/qt.

file(READ "${CMAKE_SOURCE_DIR}/tools/build/qt.json" LCE_QT_PINS)
string(JSON LCE_QT_VERSION GET "${LCE_QT_PINS}" version)
if(WIN32)
  set(LCE_QT_HOST windows)
elseif(APPLE)
  set(LCE_QT_HOST darwin)
else()
  set(LCE_QT_HOST linux)
endif()
string(JSON LCE_QT_FOLDER GET "${LCE_QT_PINS}" hosts ${LCE_QT_HOST} folder)
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/qt/${LCE_QT_VERSION}/${LCE_QT_FOLDER}")

find_package(Qt6 ${LCE_QT_VERSION} QUIET COMPONENTS Widgets Network)
if(NOT Qt6_FOUND)
  message(FATAL_ERROR "Qt ${LCE_QT_VERSION} was not found for the launcher. Run:\n"
                      "  python tools/build/qt.py\n"
                      "or configure with -DLCE_BUILD_LAUNCHER=OFF to build only the game.")
endif()

qt_standard_project_setup()

qt_add_executable(LCERebornLauncher WIN32 MACOSX_BUNDLE
  BlockIcons.cpp
  BuildVersion.cpp
  GameArt.cpp
  GameFiles.cpp
  GameLog.cpp
  GameRunner.cpp
  LauncherPaths.cpp
  MainWindow.cpp
  NewsTab.cpp
  ProfileDialog.cpp
  ProfilePopup.cpp
  Profiles.cpp
  ProfilesTab.cpp
  SetupPage.cpp
  Skins.cpp
  Theme.cpp
  UpdateCheck.cpp
  Widgets.cpp
  main.cpp)

set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/launcher/icon.png" PROPERTIES QT_RESOURCE_ALIAS icon.png)
set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/pc/icon.png" PROPERTIES QT_RESOURCE_ALIAS mark.png)
qt_add_resources(LCERebornLauncher launcher_resources
  PREFIX /launcher
  FILES "${CMAKE_SOURCE_DIR}/resources/launcher/icon.png" "${CMAKE_SOURCE_DIR}/resources/pc/icon.png")

add_custom_target(lce_launcher_build_info
  COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR="${CMAKE_SOURCE_DIR}" -DOUTPUT="${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.hpp"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.cmake"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.hpp")
add_dependencies(LCERebornLauncher lce_launcher_build_info)
target_include_directories(LCERebornLauncher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/source")
target_link_libraries(LCERebornLauncher PRIVATE Qt6::Widgets Qt6::Network)
set_target_properties(LCERebornLauncher PROPERTIES
  CXX_STANDARD 17
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(WIN32)
  target_link_libraries(LCERebornLauncher PRIVATE winmm)
  set(LCE_LAUNCHER_ICON "${CMAKE_SOURCE_DIR}/resources/launcher/icon.ico")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LCERebornLauncher.rc" "1 ICON \"${LCE_LAUNCHER_ICON}\"\n")
  target_sources(LCERebornLauncher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LCERebornLauncher.rc")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/LCERebornLauncher.rc" PROPERTIES
    OBJECT_DEPENDS "${LCE_LAUNCHER_ICON}")

  get_target_property(LCE_QMAKE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(LCE_QT_BIN "${LCE_QMAKE}" DIRECTORY)
  add_custom_command(TARGET LCERebornLauncher POST_BUILD
    COMMAND "${LCE_QT_BIN}/windeployqt.exe" --no-translations --no-compiler-runtime --no-system-d3d-compiler
            --no-system-dxc-compiler --no-opengl-sw --no-quick-import --verbose 0 "$<TARGET_FILE:LCERebornLauncher>")
elseif(NOT APPLE)
  # The Linux package keeps Qt's libraries in lib/ beside the launcher (tools/package_pc.py).
  set_target_properties(LCERebornLauncher PROPERTIES BUILD_RPATH "\$ORIGIN/lib")
endif()

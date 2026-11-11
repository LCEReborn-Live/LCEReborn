cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(LCEReborn LANGUAGES C CXX)

set(LCE_THIRD_PARTY "${CMAKE_SOURCE_DIR}/build/third_party" CACHE PATH
    "Third-party sources laid out by tools/build/deps.py and tools/build/layout.py")

if(NOT EXISTS "${LCE_THIRD_PARTY}/boost/boost")
  message(FATAL_ERROR "Third-party sources not found in ${LCE_THIRD_PARTY}. Run:\n"
                      "  python tools/build/deps.py build/deps\n"
                      "  python tools/build/layout.py build/deps build/third_party")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LCE_SOURCE "${CMAKE_SOURCE_DIR}/source")

# Settings every unit of the PC build shares.
add_library(lce_common INTERFACE)

target_include_directories(lce_common INTERFACE
  "${LCE_SOURCE}"
  "${LCE_THIRD_PARTY}/curl/include"
  "${LCE_THIRD_PARTY}/stb"
  "${LCE_THIRD_PARTY}/zlib"
  "${LCE_THIRD_PARTY}/libpng-wiiu"
  "${LCE_THIRD_PARTY}/libpng"
  "${LCE_THIRD_PARTY}/rapidxml")
target_include_directories(lce_common SYSTEM INTERFACE "${LCE_THIRD_PARTY}/boost")

target_compile_definitions(lce_common INTERFACE
  LCE_PLATFORM_PC=1
  NDEBUG
  BOOST_ENABLE_ASSERT_HANDLER
  BOOST_NO_EXCEPTIONS
  $<$<CONFIG:Debug,RelWithDebInfo>:LCE_DEBUG_BUILD=1>)

if(MSVC)
  target_compile_definitions(lce_common INTERFACE
    _HAS_EXCEPTIONS=0
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
  # Debug information for the crash reports' stacks, without changing the optimised code.
  target_compile_options(lce_common INTERFACE /EHs-c- /GR /utf-8 /bigobj /Z7)
else()
  target_compile_options(lce_common INTERFACE -fno-exceptions -frtti -fsigned-char)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(lce_common INTERFACE -Wno-c++11-narrowing)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(lce_common INTERFACE -Wno-narrowing)
endif()

# SDL3, for the window, input and audio of the PC layer.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
  URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)

# Opus keeps game voice traffic small enough for relay sessions while keeping end-to-end latency low.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus
  URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
  URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(opus)

# The game core, in the C++ dialect it was written in.
file(GLOB_RECURSE LCE_GAME_SOURCES CONFIGURE_DEPENDS "${LCE_SOURCE}/game/*.cpp")
# Units under game/ that the PC build does not compile: the Miles sound engine and the game's glue to it (PC audio
# is platform/pc/audio), the Wii U NEX and NQR online stack and the Mii picker.
set(LCE_WIIU_ONLY_GAME_SOURCES
  game/client/audio/EventSystem.cpp
  game/client/audio/FalloffGraph.cpp
  game/client/audio/PlatformSoundEngineImpl.cpp
  game/core/PODHandleArray.cpp
  game/util/LegacyUtilityFunctions.cpp
  game/core/NS_JobConnectEndPoint_CPP.cpp
  game/core/NS_Job_CPP.cpp
  game/core/NS_SystemChecker_CPP__SystemCheckFunctor.cpp
  game/core/SampleNotificationEventHandler.cpp
  game/network/INQRNetworkManagerListener.cpp
  game/ui/UIScene_AvatarSelectMenu.cpp)
foreach(unit ${LCE_WIIU_ONLY_GAME_SOURCES})
  list(REMOVE_ITEM LCE_GAME_SOURCES "${LCE_SOURCE}/${unit}")
endforeach()
# The vendor units the game core uses that are not Wii U middleware: its own data structures and stb_truetype.
set(LCE_VENDOR_SOURCES
  vendor/_ConnectionProgressParams.cpp
  vendor/_LoadingInputParams.cpp
  vendor/_MapDataMappings.cpp
  vendor/_MapDataMappings_old.cpp
  vendor/_NetworkGameInitData.cpp
  vendor/_Polygon.cpp
  vendor/_SaveListDetails.cpp
  vendor/_SkinAdjustments.cpp
  vendor/_TutorialPopupInfo.cpp
  vendor/_XPARTY_USER_INFO.cpp
  vendor/_XUSER_SIGNIN_INFO.cpp
  vendor/stb_truetype.cpp)
foreach(unit ${LCE_VENDOR_SOURCES})
  list(APPEND LCE_GAME_SOURCES "${LCE_SOURCE}/${unit}")
endforeach()
add_library(lce_game OBJECT ${LCE_GAME_SOURCES})
target_link_libraries(lce_game PRIVATE lce_common)
set_target_properties(lce_game PROPERTIES CXX_STANDARD 14)

# The PC platform layer.
file(GLOB_RECURSE LCE_PLATFORM_SOURCES CONFIGURE_DEPENDS "${LCE_SOURCE}/platform/pc/*.cpp")
list(FILTER LCE_PLATFORM_SOURCES EXCLUDE REGEX "/platform/pc/audio/")
# Each program's entry point: the game's Main.cpp and the dedicated server's ServerMain.cpp.
list(FILTER LCE_PLATFORM_SOURCES EXCLUDE REGEX "/platform/pc/(Server)?Main\\.cpp$")
# The LAN and internet play shared with the Wii U build.
file(GLOB LCE_NET_SOURCES CONFIGURE_DEPENDS "${LCE_SOURCE}/net/*.cpp")
list(APPEND LCE_PLATFORM_SOURCES ${LCE_NET_SOURCES})
# Units under platform/wiiu shared with PC: 4J's memory helpers and System (whose clock is switched per platform).
list(APPEND LCE_PLATFORM_SOURCES
  "${LCE_SOURCE}/platform/wiiu/Memory.cpp"
  "${LCE_SOURCE}/platform/wiiu/System.cpp")
add_library(lce_platform OBJECT ${LCE_PLATFORM_SOURCES})
target_link_libraries(lce_platform PRIVATE lce_common lce_zlib SDL3::SDL3-static)
set_target_properties(lce_platform PROPERTIES CXX_STANDARD 17)
add_custom_target(lce_build_info
  COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR="${CMAKE_SOURCE_DIR}" -DOUTPUT="${CMAKE_BINARY_DIR}/generated/BuildInfo.hpp"
          -P "${CMAKE_SOURCE_DIR}/launcher/BuildInfo.cmake"
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/BuildInfo.hpp")
add_dependencies(lce_platform lce_build_info)
set(LCE_DISCORD_APP_ID "" CACHE STRING "Discord application used for Rich Presence when --discord-app is not given")
target_compile_definitions(lce_platform PRIVATE LCE_DISCORD_APP_ID="${LCE_DISCORD_APP_ID}")
target_include_directories(lce_platform PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(MSVC)
  # Boost 1.53 still uses std::auto_ptr and std::unary_function, which C++17 removes.
  target_compile_definitions(lce_platform PRIVATE _HAS_AUTO_PTR_ETC=1)
endif()

# Audio: the game's sound bank and Bink Audio, played through SDL.
file(GLOB_RECURSE LCE_PC_AUDIO_SOURCES CONFIGURE_DEPENDS "${LCE_SOURCE}/platform/pc/audio/*.cpp")
add_library(lce_audio OBJECT ${LCE_PC_AUDIO_SOURCES})
target_link_libraries(lce_audio PRIVATE lce_common SDL3::SDL3-static opus)
set_target_properties(lce_audio PROPERTIES CXX_STANDARD 17)

# zlib and libpng, the releases the game was built against (libpng with the game's own png.h and pngwrite.c).
set(LCE_ZLIB_SOURCES adler32.c compress.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c trees.c
                     uncompr.c zutil.c)
list(TRANSFORM LCE_ZLIB_SOURCES PREPEND "${LCE_THIRD_PARTY}/zlib/")
add_library(lce_zlib STATIC ${LCE_ZLIB_SOURCES})
target_include_directories(lce_zlib PUBLIC "${LCE_THIRD_PARTY}/zlib")

set(LCE_PNG_SOURCES png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c
                    pngtrans.c pngwio.c pngwtran.c pngwutil.c)
list(TRANSFORM LCE_PNG_SOURCES PREPEND "${LCE_THIRD_PARTY}/libpng/")
add_library(lce_png STATIC ${LCE_PNG_SOURCES} "${LCE_THIRD_PARTY}/libpng-wiiu/pngwrite.c")
target_include_directories(lce_png PUBLIC "${LCE_THIRD_PARTY}/libpng-wiiu" "${LCE_THIRD_PARTY}/libpng")
target_link_libraries(lce_png PUBLIC lce_zlib)

if(MSVC)
  target_compile_definitions(lce_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(lce_png PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

function(lce_add_program target main)
  add_executable(${target} "${LCE_SOURCE}/platform/pc/${main}" $<TARGET_OBJECTS:lce_game>
                 $<TARGET_OBJECTS:lce_platform> $<TARGET_OBJECTS:lce_audio>)
  target_link_libraries(${target} PRIVATE lce_common lce_png lce_zlib SDL3::SDL3-static opus)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 17)
  target_compile_definitions(${target} PRIVATE LCE_DISCORD_APP_ID="${LCE_DISCORD_APP_ID}")
  if(WIN32)
    target_link_libraries(${target} PRIVATE shell32 ws2_32 dbghelp)
    # The executable's icon (tools/pcart/make_icon.py).
    target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/LCEReborn.rc")
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(${target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    # Function names in crash report stacks.
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON)
  endif()
  if(MSVC)
    target_compile_definitions(${target} PRIVATE _HAS_AUTO_PTR_ETC=1)
    target_link_options(${target} PRIVATE /errorlimit:0 /DEBUG /OPT:REF /OPT:ICF)
  endif()
endfunction()

if(WIN32)
  set(LCE_ICON "${CMAKE_SOURCE_DIR}/resources/pc/icon.ico")
  file(WRITE "${CMAKE_BINARY_DIR}/LCEReborn.rc" "1 ICON \"${LCE_ICON}\"\n")
  set_source_files_properties("${CMAKE_BINARY_DIR}/LCEReborn.rc" PROPERTIES OBJECT_DEPENDS "${LCE_ICON}")
endif()

lce_add_program(LCEReborn Main.cpp)
lce_add_program(LCERebornServer ServerMain.cpp)
# The PC interface art (tools/pcart) is read from resources/ in the working folder unless --resource-dir is given.
add_custom_command(TARGET LCEReborn POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/pc"
          "$<TARGET_FILE_DIR:LCEReborn>/resources")

option(LCE_BUILD_LAUNCHER "Build the Qt launcher (needs python tools/build/qt.py first)" ON)
if(LCE_BUILD_LAUNCHER)
  add_subdirectory(launcher)
endif()

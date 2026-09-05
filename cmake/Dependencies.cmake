include_guard(GLOBAL)
include(FetchContent)

set(RAYLIB_DIR "$ENV{RAYLIB_DIR}" CACHE PATH "raylib source directory (or Windows installer root); empty downloads pinned release")
set(BOX3D_DIR "${PROJECT_SOURCE_DIR}/third_party/box3d" CACHE PATH "Box3D source directory")
option(NG_FETCH_DEPENDENCIES "Download pinned dependencies when no local source is supplied" ON)
set(NG_RAYLIB_REVISION "6.0" CACHE STRING "raylib release tag or commit to download")

if(NOT EXISTS "${BOX3D_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Box3D sources missing at '${BOX3D_DIR}'. Restore third_party/box3d or set -DBOX3D_DIR=<source-directory>.")
endif()

macro(ng_add_raylib)
  set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples")
  set(BUILD_GAMES OFF CACHE BOOL "Build raylib games")
  if(RAYLIB_DIR)
    # Accept the root of the official Windows installer as well as a source tree.
    if(NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt" AND EXISTS "${RAYLIB_DIR}/raylib/CMakeLists.txt")
      set(RAYLIB_DIR "${RAYLIB_DIR}/raylib")
    endif()
    if(NOT EXISTS "${RAYLIB_DIR}/src/raylib.h" OR NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "Invalid RAYLIB_DIR='${RAYLIB_DIR}'. Pass -DRAYLIB_DIR=C:/raylib/raylib-6.0 (source tree), or -DRAYLIB_DIR= to download raylib.")
    endif()
    add_subdirectory("${RAYLIB_DIR}" "${CMAKE_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)
  elseif(NG_FETCH_DEPENDENCIES)
    FetchContent_Declare(raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG "${NG_RAYLIB_REVISION}"
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
    set(RAYLIB_DIR "${raylib_SOURCE_DIR}")
  else()
    message(FATAL_ERROR "raylib is required. Set -DRAYLIB_DIR=<source-directory> or enable NG_FETCH_DEPENDENCIES.")
  endif()
  message(STATUS "ngame raylib source: ${RAYLIB_DIR}")
  message(STATUS "ngame Box3D source: ${BOX3D_DIR}")
endmacro()

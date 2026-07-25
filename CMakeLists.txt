# agent: composer-2.5 | 2026-07-25 | ngame cmake raylib6 | 8f3a1c
cmake_minimum_required(VERSION 3.20)
project(ngame C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED PLATFORM)
  set(PLATFORM Desktop CACHE STRING "raylib platform")
endif()

set(RAYLIB_DIR /home/x/code/raylib)
add_subdirectory(${RAYLIB_DIR} ${CMAKE_BINARY_DIR}/raylib EXCLUDE_FROM_ALL)

set(NG_SOURCES
  src/main.c
  src/ng_app.c
  src/ng_console.c
  src/ng_viewport.c
  src/ng_shader.c
  src/ng_scene.c
  src/ng_scene_sphere.c
  src/ng_scene_cube.c
  src/ng_cli.c
  src/ng_script.c
  src/ng_bind.c
  src/vendor/duktape.c
)

add_executable(ngame ${NG_SOURCES})
target_link_libraries(ngame PRIVATE raylib m)
target_include_directories(ngame PRIVATE src src/vendor)
target_compile_definitions(ngame PRIVATE NG_RES_ROOT=\"res/\")

if(EMSCRIPTEN)
  set_target_properties(ngame PROPERTIES SUFFIX ".html")
  target_link_options(ngame PRIVATE
    --shell-file ${CMAKE_SOURCE_DIR}/shell.html
    --preload-file ${CMAKE_SOURCE_DIR}/res@/res
    -sASYNCIFY
  )
else()
  add_custom_command(TARGET ngame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ngame>/res
    COMMENT "Copy res/ next to ngame binary")
endif()

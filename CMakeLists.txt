# agent: composer-2.5 | 2026-07-25 | emscripten websocket link | a7c19d
# agent: composer-2.5 | 2026-07-25 | enet ipv4 only compile def | d821f1
cmake_minimum_required(VERSION 3.20)
project(ngame C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED PLATFORM)
  set(PLATFORM Desktop CACHE STRING "raylib platform")
endif()

set(RAYLIB_DIR /home/x/code/raylib)
add_subdirectory(${RAYLIB_DIR} ${CMAKE_BINARY_DIR}/raylib EXCLUDE_FROM_ALL)

set(NG_CORE_SOURCES
  src/core/ng_bus.c
  src/core/ng_mod.c
  src/core/ng_proto.c
  src/world/ng_world.c
)

set(NG_SIM_SOURCES
  src/sim/sim_sphere.c
  src/sim/sim_cube.c
  src/mod/mod_sim.c
)

set(NG_VENDOR_SOURCES
  src/vendor/duktape.c
)

set(NG_CLIENT_SOURCES
  src/client/main_client.c
  src/client/ng_app_client.c
  src/mod/mod_console.c
  src/mod/mod_net.c
  src/mod/mod_render.c
  src/ng_viewport.c
  src/ng_shader.c
)

set(NG_SERVER_SOURCES
  src/server/main_server.c
  src/server/ng_app_server.c
  src/mod/mod_script.c
  src/mod/mod_net.c
  src/mod/mod_agent.c
  src/net/ng_ws_server.c
  ${NG_SIM_SOURCES}
  ${NG_VENDOR_SOURCES}
)

if(EMSCRIPTEN)
  list(APPEND NG_CLIENT_SOURCES src/net/ng_net_ws.c)
else()
  list(APPEND NG_CLIENT_SOURCES src/net/ng_net_enet.c)
  list(APPEND NG_SERVER_SOURCES src/net/ng_net_enet.c)
  set(NG_ENET_DEFS ENET_IPV4_ONLY)
endif()

add_executable(ngame ${NG_CORE_SOURCES} ${NG_CLIENT_SOURCES})
target_link_libraries(ngame PRIVATE raylib m)
target_include_directories(ngame PRIVATE src src/vendor)
target_compile_definitions(ngame PRIVATE NG_RES_ROOT=\"res/\" ${NG_ENET_DEFS})

if(EMSCRIPTEN)
  set_target_properties(ngame PROPERTIES SUFFIX ".html")
  target_link_options(ngame PRIVATE
    --shell-file ${CMAKE_SOURCE_DIR}/shell.html
    --preload-file ${CMAKE_SOURCE_DIR}/res@/res
    -sASYNCIFY
    -lwebsocket
  )
else()
  add_custom_command(TARGET ngame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ngame>/res
    COMMENT "Copy res/ next to ngame binary")
endif()

if(NOT EMSCRIPTEN)
  add_executable(ngame_server ${NG_CORE_SOURCES} ${NG_SERVER_SOURCES})
  target_link_libraries(ngame_server PRIVATE m)
  target_include_directories(ngame_server PRIVATE src src/vendor)
  target_compile_definitions(ngame_server PRIVATE NG_SERVER NG_RES_ROOT=\"res/\" ${NG_ENET_DEFS})
  add_custom_command(TARGET ngame_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ngame_server>/res
    COMMENT "Copy res/ next to ngame_server binary")

  add_executable(ng_test_net tools/net_smoke.c src/core/ng_proto.c src/net/ng_net_enet.c)
  # agent: composer-2.5 | 2026-07-25 | ng_test_net smoke stderr log | 6fe5db
  target_include_directories(ng_test_net PRIVATE src src/vendor)
  target_compile_definitions(ng_test_net PRIVATE NG_NET_SMOKE ${NG_ENET_DEFS})
  target_link_libraries(ng_test_net PRIVATE m)
endif()

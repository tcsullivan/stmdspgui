cmake_minimum_required(VERSION 3.10)

project(stmdspgui VERSION 0.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O0 -ggdb -g3 -Wall -Wextra -pedantic)

file(GLOB SRC_IMGUI_BACKENDS "${CMAKE_SOURCE_DIR}/source/imgui/backends/*.cpp")
file(GLOB SRC_IMGUI "${CMAKE_SOURCE_DIR}/source/imgui/*.cpp")
file(GLOB SRC_STMDSP "${CMAKE_SOURCE_DIR}/source/stmdsp/*.cpp")
file(GLOB SRC_STMDSPGUI "${CMAKE_SOURCE_DIR}/source/*.cpp")

add_executable(stmdspgui
    source/serial/src/serial.cc
    source/serial/src/impl/unix.cc
    source/serial/src/impl/list_ports/list_ports_linux.cc
    ${SRC_IMGUI_BACKENDS}
    ${SRC_IMGUI}
    ${SRC_STMDSP}
    ${SRC_STMDSPGUI})

target_include_directories(stmdspgui PUBLIC
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/source/imgui
    ${CMAKE_SOURCE_DIR}/source/stmdsp
    ${CMAKE_SOURCE_DIR}/source/serial/include)

target_link_libraries(stmdspgui PRIVATE SDL2 GL pthread)


cmake_minimum_required(VERSION 3.16)
project(ultimalauncher_raylib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(raylib REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)

add_executable(ultimalauncher-raylib main.cpp)
target_link_libraries(ultimalauncher-raylib PRIVATE raylib PkgConfig::JSONCPP)
target_compile_options(ultimalauncher-raylib PRIVATE -Wall -Wextra -Wpedantic)
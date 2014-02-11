project(SparksCore)

cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/../cmake_modules")

find_package(PkgConfig REQUIRED)
find_package(Glog REQUIRED)

set(CMAKE_CXX_FLAGS "-std=c++11 -Wall")
set(CMAKE_LD_FLAGS "-lRegal -lRegalGLU")

pkg_search_module(SDL2 REQUIRED sdl2)
include_directories(
  ${SDL2_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
)

add_library(
  sparks-core
    executor.cpp
    window.cpp
    application.cpp
)
target_link_libraries(
  sparks-core
    ${SDL2_LIBRARIES}
    ${GLOG_LIBRARY}
)

add_executable(sdl_sandbox test.cpp)
target_link_libraries(
  sdl_sandbox
    sparks-core
    ${SDL2_LIBRARIES}
    ${GLOG_LIBRARY}
)


project(SparksCore)

cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/../cmake_modules")

find_package(PkgConfig REQUIRED)
find_package(Glog REQUIRED)

set(CMAKE_CXX_FLAGS "-g -pthread -std=c++11 -Wall")
set(CMAKE_LD_FLAGS "-pthread -lRegal -lRegalGLU")

pkg_search_module(SDL2 REQUIRED sdl2)
include_directories(
  ${SDL2_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
)


add_library(
  sparks-core
    application.cpp
    application.hpp
    arraydelegate.hpp
    blocking_counter.hpp
    executor.cpp
    executor.hpp
    id_vector_fwd.hpp
    id_vector.hpp
    spin_lock.hpp
    stable_id_vector_fwd.hpp
    stable_id_vector.hpp
    window.cpp
    window.hpp
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


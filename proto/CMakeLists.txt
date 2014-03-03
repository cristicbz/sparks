project(SparksCore)

cmake_minimum_required(VERSION 2.8)


set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/../cmake_modules")

find_package(PkgConfig REQUIRED)
find_package(Glog REQUIRED)
find_package(GTest REQUIRED)

message(STATUS ${GTEST_LIBRARIES})

set(CMAKE_CXX_FLAGS "-g -pthread -std=c++11 -Wall")
set(CMAKE_LD_FLAGS "-pthread -lRegal -lRegalGLU")

pkg_search_module(SDL2 REQUIRED sdl2)
include_directories(
  ${SDL2_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${GLOG_INCLUDE_DIRS}
  ${GTEST_INCLUDE_DIRS}
)

add_library(
  sparks
    id_vector.hpp
    sparks.cpp
)
target_link_libraries(
  sparks
    ${SDL2_LIBRARIES}
    ${GLOG_LIBRARY}
)

add_executable(sandbox sandbox.cpp)
target_link_libraries(
  sandbox
    sparks
    ${SDL2_LIBRARIES}
    ${GLOG_LIBRARY}
)


file(GLOB TEST_SRC_FILES *_test.cpp)
add_executable(
  unittests
    unitests_main.cpp
    ${TEST_SRC_FILES}
)
target_link_libraries(
  unittests
    sparks
    ${GTEST_LIBRARIES}
)


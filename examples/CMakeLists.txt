# -*- mode: cmake; -*-
# vi: set ft=cmake:

#
# examples/CMakeLists.txt
# RVO2 Library
#
# SPDX-FileCopyrightText: 2008 University of North Carolina at Chapel Hill
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Please send all bug reports to <geom@cs.unc.edu>.
#
# The authors may be contacted via:
#
# Jur van den Berg, Stephen J. Guy, Jamie Snape, Ming C. Lin, Dinesh Manocha
# Dept. of Computer Science
# 201 S. Columbia St.
# Frederick P. Brooks, Jr. Computer Science Bldg.
# Chapel Hill, N.C. 27599-3175
# United States of America
#
# <https://gamma.cs.unc.edu/RVO2/>
#

option(BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})

if(BUILD_EXAMPLES OR ENABLE_RAYLIB_EXAMPLE)
  add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/examples/resources
      ${CMAKE_SOURCE_DIR}/bin/resources
    COMMENT "Copying resources to bin/resources")
endif()

if(BUILD_EXAMPLES)
  option(OUTPUT_TIME_AND_POSITIONS "Output time and positions" ON)

  if(OUTPUT_TIME_AND_POSITIONS)
    set(RVO_EXAMPLES_COMPILE_DEFINITIONS RVO_OUTPUT_TIME_AND_POSITIONS=1)
  else()
    set(RVO_EXAMPLES_COMPILE_DEFINITIONS RVO_OUTPUT_TIME_AND_POSITIONS=0)
  endif()

  option(SEED_RANDOM_NUMBER_GENERATOR "Seed random number generator" ON)

  if(SEED_RANDOM_NUMBER_GENERATOR)
    list(APPEND RVO_EXAMPLES_COMPILE_DEFINITIONS
      RVO_SEED_RANDOM_NUMBER_GENERATOR=1)
  else()
    list(APPEND RVO_EXAMPLES_COMPILE_DEFINITIONS
      RVO_SEED_RANDOM_NUMBER_GENERATOR=0)
  endif()

  if(ENABLE_INTERPROCEDURAL_OPTIMIZATION
      AND RVO_INTERPROCEDURAL_OPTIMIZATION_SUPPORTED)
    set(RVO_EXAMPLES_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    set(RVO_EXAMPLES_INTERPROCEDURAL_OPTIMIZATION OFF)
  endif()

  include(CheckPIESupported)
  check_pie_supported()

  add_executable(Blocks Blocks.cc)
  target_compile_definitions(Blocks PRIVATE
    ${RVO_EXAMPLES_COMPILE_DEFINITIONS})
  target_link_libraries(Blocks PRIVATE ${RVO_LIBRARY})
  if(ENABLE_OPENMP AND OpenMP_FOUND)
    target_link_libraries(Blocks PRIVATE OpenMP::OpenMP_CXX)
  endif()
  set_target_properties(Blocks PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION ${RVO_EXAMPLES_INTERPROCEDURAL_OPTIMIZATION})
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.1)
    set_target_properties(Blocks PROPERTIES
      SPDX_LICENSE Apache-2.0)
  endif()
  add_test(NAME Blocks COMMAND Blocks)
  set_tests_properties(Blocks PROPERTIES
    LABELS medium
    TIMEOUT 60)

  add_executable(Circle Circle.cc)
  target_compile_definitions(Circle PRIVATE
    ${RVO_EXAMPLES_COMPILE_DEFINITIONS})
  target_link_libraries(Circle PRIVATE ${RVO_LIBRARY})
  if(ENABLE_OPENMP AND OpenMP_FOUND)
    target_link_libraries(Circle PRIVATE OpenMP::OpenMP_CXX)
  endif()
  set_target_properties(Circle PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION ${RVO_EXAMPLES_INTERPROCEDURAL_OPTIMIZATION})
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.1)
    set_target_properties(Circle PROPERTIES
      SPDX_LICENSE Apache-2.0)
  endif()
  add_test(NAME Circle COMMAND Circle)
  set_tests_properties(Circle PROPERTIES
    LABELS medium
    TIMEOUT 60)

  add_executable(Roadmap Roadmap.cc)
  target_compile_definitions(Roadmap PRIVATE
    ${RVO_EXAMPLES_COMPILE_DEFINITIONS})
  target_link_libraries(Roadmap PRIVATE ${RVO_LIBRARY})
  if(ENABLE_OPENMP AND OpenMP_FOUND)
    target_link_libraries(Roadmap PRIVATE OpenMP::OpenMP_CXX)
  endif()
  set_target_properties(Roadmap PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION ${RVO_EXAMPLES_INTERPROCEDURAL_OPTIMIZATION})
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.1)
    set_target_properties(Roadmap PROPERTIES
      SPDX_LICENSE Apache-2.0)
  endif()
  add_test(NAME Roadmap COMMAND Roadmap)
  set_tests_properties(Roadmap PROPERTIES
    LABELS medium
    TIMEOUT 60)
endif()

if(ENABLE_RAYLIB_EXAMPLE)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/libs/raylib/CMakeLists.txt")
    message(FATAL_ERROR
      "Raylib submodule not initialized. Please run:\n"
      "  git submodule update --init --recursive")
  endif()

  set(RVO_PREV_BUILD_EXAMPLES ${BUILD_EXAMPLES})
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

  if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
  endif()

  set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_SOURCE_DIR}/libs/raylib
    ${CMAKE_BINARY_DIR}/libs/raylib)
  set(BUILD_EXAMPLES ${RVO_PREV_BUILD_EXAMPLES} CACHE BOOL "" FORCE)
  set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

  function(add_rvo_raylib_example target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${RVO_LIBRARY} raylib)
    if(ENABLE_OPENMP AND OpenMP_FOUND)
      target_link_libraries(${target_name} PRIVATE OpenMP::OpenMP_CXX)
    endif()
    set_target_properties(${target_name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
  endfunction()

  file(GLOB RVO_RAYLIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/raylib_*.cpp")

  foreach(RVO_RAYLIB_SOURCE ${RVO_RAYLIB_SOURCES})
    get_filename_component(RVO_RAYLIB_TARGET ${RVO_RAYLIB_SOURCE} NAME_WE)
    add_rvo_raylib_example(${RVO_RAYLIB_TARGET} ${RVO_RAYLIB_SOURCE})
    message(STATUS "  Adding raylib example: ${RVO_RAYLIB_TARGET}")
  endforeach()
endif()

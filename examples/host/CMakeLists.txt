# =============================================================================
# CANOPEN_CLIENT driver: host example (any PC, no hardware)
# =============================================================================
# The driver against a simulated CANOPEN_CLIENT (the register model the host tests
# use, tests/mock_canopen_client.hpp) through the host port in this folder.
#
#   cmake -S examples/host -B build-example
#   cmake --build build-example && ./build-example/canopen_client_host_example
#
# The host tests also build this folder and run the example (CTest
# canopen_client_host_example), so it always compiles against the current headers.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(canopen_client_host_example LANGUAGES CXX)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. canopen_client)
endif()

# ISO C++20, also when this folder is built on its own (handbook KB-CPP20-001).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(canopen_client_host_example main.cpp)
target_link_libraries(canopen_client_host_example PRIVATE canopen_client::canopen_client)
target_include_directories(canopen_client_host_example PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../../tests)
target_compile_features(canopen_client_host_example PRIVATE cxx_std_20)
target_compile_options(canopen_client_host_example PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
  -pedantic-errors)

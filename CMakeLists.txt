#===============================================================================
# CANOPEN_CLIENT driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   canopen_client, consumed as canopen_client::canopen_client both through add_subdirectory() and
#   through find_package(canopen_client) after install.
#
#   cmake -S . -B build -DCANOPEN_CLIENT_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/canopen_client_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${CANOPEN_CLIENT_SOURCE_FILES}
        INCLUDE_DIRS ${CANOPEN_CLIENT_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${CANOPEN_CLIENT_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(CANOPEN_CLIENT_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(canopen_client_driver
    VERSION ${CANOPEN_CLIENT_VERSION}
    DESCRIPTION "A generic, product-agnostic CANopen client: NMT master, SDO client, heartbeat, EMCY, SYNC, PDO helpers and a CiA 402 drive state machine over a classic CAN frame port."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because canopen_client.hpp includes
# "../src/canopen_client.ipp" relative to itself:
#   <prefix>/include/canopen_client/inc/*.hpp, canopen_client_version.h
#   <prefix>/include/canopen_client/src/canopen_client.ipp
set(CANOPEN_CLIENT_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/canopen_client/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${CANOPEN_CLIENT_TARGET_NAME} INTERFACE)
target_include_directories(${CANOPEN_CLIENT_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${CANOPEN_CLIENT_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${CANOPEN_CLIENT_INSTALL_INCLUDEDIR}>
)
target_compile_features(${CANOPEN_CLIENT_TARGET_NAME} INTERFACE cxx_std_20)

# canopen_client::canopen_client everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(canopen_client::canopen_client ALIAS ${CANOPEN_CLIENT_TARGET_NAME})
set_target_properties(${CANOPEN_CLIENT_TARGET_NAME} PROPERTIES EXPORT_NAME canopen_client)

option(CANOPEN_CLIENT_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links canopen_client::canopen_client" OFF)
if(CANOPEN_CLIENT_ENABLE_WARNINGS)
    target_compile_options(${CANOPEN_CLIENT_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(CANOPEN_CLIENT_BUILD_TESTS "Build the host tests in tests/" OFF)
option(CANOPEN_CLIENT_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(CANOPEN_CLIENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${CANOPEN_CLIENT_TARGET_NAME} EXPORT canopen_clientTargets
    INCLUDES DESTINATION ${CANOPEN_CLIENT_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${CANOPEN_CLIENT_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${CANOPEN_CLIENT_VERSION_HEADER}" DESTINATION ${CANOPEN_CLIENT_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/canopen_client/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT canopen_clientTargets
    FILE canopen_clientTargets.cmake
    NAMESPACE canopen_client::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/canopen_client)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/canopen_clientConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/canopen_clientConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/canopen_client)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/canopen_clientConfigVersion.cmake"
    VERSION ${CANOPEN_CLIENT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/canopen_clientConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/canopen_clientConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/canopen_client)

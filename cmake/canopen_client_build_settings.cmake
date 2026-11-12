#===============================================================================
# CANOPEN_CLIENT driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(CANOPEN_CLIENT_TARGET_NAME "canopen_client")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(CANOPEN_CLIENT_VERSION_MAJOR 0)
set(CANOPEN_CLIENT_VERSION_MINOR 1)
set(CANOPEN_CLIENT_VERSION_PATCH 0)
set(CANOPEN_CLIENT_VERSION "${CANOPEN_CLIENT_VERSION_MAJOR}.${CANOPEN_CLIENT_VERSION_MINOR}.${CANOPEN_CLIENT_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(CANOPEN_CLIENT_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/canopen_client_version.h.in")
set(CANOPEN_CLIENT_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/canopen_client_generated")
set(CANOPEN_CLIENT_VERSION_HEADER "${CANOPEN_CLIENT_VERSION_HEADER_DIR}/canopen_client_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${CANOPEN_CLIENT_VERSION_HEADER_DIR}")
configure_file("${CANOPEN_CLIENT_VERSION_TEMPLATE}" "${CANOPEN_CLIENT_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(CANOPEN_CLIENT_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${CANOPEN_CLIENT_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/canopen_client.ipp, included by inc/canopen_client.hpp.
set(CANOPEN_CLIENT_SOURCE_FILES)

set(CANOPEN_CLIENT_IDF_REQUIRES)

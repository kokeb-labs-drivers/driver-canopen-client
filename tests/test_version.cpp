/**
 * @file test_version.cpp
 * @brief The version getters follow the generated canopen_client_version.h, so they cannot drift
 * from CMake.
 *
 * Built twice by tests/CMakeLists.txt: against the real generated header (the getters must equal
 * the CMake project version, passed as EXPECTED_*), and against a probe header generated from the
 * same template with version 9.8.7 (a getter that returns a literal instead of reading the header
 * fails there).
 */
#include <cstdio>
#include <cstring>

#include "canopen_client_types.hpp"

static_assert(canopen_client::GetDriverVersionMajor() == EXPECTED_MAJOR,
              "major version differs from CMake");
static_assert(canopen_client::GetDriverVersionMinor() == EXPECTED_MINOR,
              "minor version differs from CMake");
static_assert(canopen_client::GetDriverVersionPatch() == EXPECTED_PATCH,
              "patch version differs from CMake");

int main() {
  if (std::strcmp(canopen_client::GetDriverVersion(), EXPECTED_STRING) != 0) {
    std::printf("version string %s, expected %s\n", canopen_client::GetDriverVersion(),
                EXPECTED_STRING);
    return 1;
  }
  std::printf("version %s: PASS\n", canopen_client::GetDriverVersion());
  return 0;
}

/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the driver is constrained by the port concept.
 *
 * EXPECT: Port
 */
#include "canopen_client.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  canopen_client::Client<NotAPort> client(port);
  return static_cast<int>(client.EnsureInitialized().error);
}

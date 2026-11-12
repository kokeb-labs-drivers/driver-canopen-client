/**
 * @file hook_not_noexcept.cpp
 * @brief Must NOT compile: a Send hook that is not noexcept breaks the port contract, so the port
 * concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: CanPort
 */
#include "canopen_client.hpp"

class ThrowingPort : public canopen_client::CanInterface<ThrowingPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Send(const canopen_client::CanFrame& frame) {
    static_cast<void>(frame);
    return true;
  }
};

int main() {
  ThrowingPort port;
  canopen_client::Client<ThrowingPort> client(port);
  return static_cast<int>(client.EnsureInitialized().error);
}

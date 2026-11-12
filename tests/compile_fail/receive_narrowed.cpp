/**
 * @file receive_narrowed.cpp
 * @brief Must NOT compile: a Receive hook with a narrower timeout would truncate long waits, so
 * the driver rejects it instead of using it (handbook KB-CPP20-007).
 *
 * EXPECT: hooks must be
 */
#include "canopen_client.hpp"

class NarrowPort : public canopen_client::CanInterface<NarrowPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    static_cast<void>(frame);
    return true;
  }
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint16_t timeout_us) noexcept {
    static_cast<void>(frame);
    static_cast<void>(timeout_us);
    return canopen_client::RxStatus::Timeout;
  }
  uint32_t NowUs() noexcept {
    return 0;
  }
};

int main() {
  NarrowPort port;
  canopen_client::Client<NarrowPort> client(port);
  return static_cast<int>(client.EnsureInitialized().error);
}

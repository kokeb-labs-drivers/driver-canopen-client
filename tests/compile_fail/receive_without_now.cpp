/**
 * @file receive_without_now.cpp
 * @brief Must NOT compile: a port that receives needs a clock for the timeouts, so `Receive` and
 * `NowUs` come as a pair.
 *
 * EXPECT: both `Receive` and `NowUs`
 */
#include "canopen_client.hpp"

class NoClockPort : public canopen_client::CanInterface<NoClockPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    static_cast<void>(frame);
    return true;
  }
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    static_cast<void>(frame);
    static_cast<void>(timeout_us);
    return canopen_client::RxStatus::Timeout;
  }
};

int main() {
  NoClockPort port;
  canopen_client::Client<NoClockPort> client(port);
  return static_cast<int>(client.EnsureInitialized().error);
}

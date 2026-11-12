/**
 * @file log_hook_wrong.cpp
 * @brief Must NOT compile: an optional hook with the wrong signature is rejected, not ignored.
 *
 * EXPECT: Log hook must be
 */
#include "canopen_client.hpp"
#include "mock_canopen_client.hpp"

class BadLogPort : public canopen_client::CanInterface<BadLogPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    static_cast<void>(frame);
    return true;
  }
  void Log(canopen_client::LogLevel level, const char* message) {
    static_cast<void>(level);
    static_cast<void>(message);
  }
};

int main() {
  BadLogPort port;
  canopen_client::Client<BadLogPort> client(port);
  return static_cast<int>(client.EnsureInitialized().error);
}

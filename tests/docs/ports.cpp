/**
 * @file ports.cpp
 * @brief The ports shown on the Porting page: transmit-only and with every optional hook. Compiled,
 * never run.
 */
#include <cstdint>

#include "canopen_client.hpp"

bool platform_can_start() noexcept;
bool platform_can_send(const canopen_client::CanFrame& frame) noexcept;
int platform_can_receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept;
uint32_t platform_micros() noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:tx-port]
// A node that only commands (a controller that cannot read the bus): two hooks, and the calls that
// need a reply do not compile.
class TxPort : public canopen_client::CanInterface<TxPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_can_start();
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    return platform_can_send(frame);
  }
};
// --8<-- [end:tx-port]

// --8<-- [start:full-port]
class FullPort : public canopen_client::CanInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_can_start();
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    return platform_can_send(frame);
  }
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    switch (platform_can_receive(frame, timeout_us)) {
    case 0:
      return canopen_client::RxStatus::Frame;
    case 1:
      return canopen_client::RxStatus::Timeout; // nothing arrived: not an error
    default:
      return canopen_client::RxStatus::Error; // bus off, controller stopped
    }
  }
  uint32_t NowUs() noexcept {
    return platform_micros();
  }

  // Optional: one line per failure. The driver never formats; the port does.
  void Log(canopen_client::LogLevel level, const char* message) noexcept {
    (void)level;
    platform_log(message); // plain text: never use it as a format string
  }
};
// --8<-- [end:full-port]

void SendOnlyUse(TxPort& port) {
  canopen_client::Client<TxPort> client(port);
  static_cast<void>(client.EnsureInitialized());
  static_cast<void>(client.SendNmt(canopen_client::NmtCommand::Start, 0)); // every node
  static_cast<void>(client.SendSync());
}

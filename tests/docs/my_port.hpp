/**
 * @file my_port.hpp
 * @brief The CAN port shown on the Quickstart and Porting pages, compiled by the host build.
 */
#ifndef CANOPEN_CLIENT_MY_PORT_HPP
#define CANOPEN_CLIENT_MY_PORT_HPP

#include <cstdint>

#include "canopen_client_can_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_can_start(uint32_t bit_rate) noexcept; // controller up at 500 kbit/s
bool platform_can_send(uint16_t id, bool rtr, uint8_t dlc, const uint8_t* data) noexcept;
// 0 frame, 1 timeout, 2 controller error; fills id, rtr, dlc and the first dlc bytes of data.
int platform_can_receive(uint32_t timeout_us, uint16_t& id, bool& rtr, uint8_t& dlc,
                         uint8_t* data) noexcept;
uint32_t platform_micros() noexcept; // a free-running microsecond counter

// --8<-- [start:port]
class MyPort : public canopen_client::CanInterface<MyPort> {
public:
  // Bring the CAN controller up at 500 kbit/s, once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_can_start(500'000U);
    }
    return ready_;
  }

  // Queue one frame: false when the controller refused it.
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    return platform_can_send(frame.id, frame.rtr, frame.dlc, frame.data.data());
  }

  // Wait up to `timeout_us` for one 11-bit frame; 0 only looks.
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    frame = canopen_client::CanFrame{};
    const int status =
        platform_can_receive(timeout_us, frame.id, frame.rtr, frame.dlc, frame.data.data());
    if (status == 0) {
      return canopen_client::RxStatus::Frame;
    }
    return (status == 1) ? canopen_client::RxStatus::Timeout : canopen_client::RxStatus::Error;
  }

  // A clock that never stops: every timeout in the driver is measured with it.
  uint32_t NowUs() noexcept {
    return platform_micros();
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // CANOPEN_CLIENT_MY_PORT_HPP

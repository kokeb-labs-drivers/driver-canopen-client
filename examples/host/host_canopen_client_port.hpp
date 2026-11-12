/**
 * @file host_canopen_client_port.hpp
 * @brief The host port: the driver's CAN hooks over a simulated bus with CANopen nodes, so the
 * example runs on any PC.
 *
 * `canopen_client_test::SimBus` (tests/mock_canopen_client.hpp) is the bus the host tests use:
 * nodes with an object dictionary, an SDO server, NMT, heartbeat, EMCY, PDOs and a CiA 402 drive.
 * This class adds what a real port adds: `Send`, `Receive` and `NowUs` (here the bus's clock moves
 * instead of sleeping) and `Log()` to the console. A port for a real CAN adapter on a PC
 * (SocketCAN, a USB adapter) implements the same hooks: see docs/porting.md.
 */
#ifndef CANOPEN_CLIENT_HOST_CANOPEN_CLIENT_PORT_HPP
#define CANOPEN_CLIENT_HOST_CANOPEN_CLIENT_PORT_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "canopen_client_can_interface.hpp"
#include "mock_canopen_client.hpp"

/// The host port: simulated bus, real console.
class HostCanopenClientPort : public canopen_client::CanInterface<HostCanopenClientPort> {
public:
  canopen_client_test::SimBus bus; ///< The simulated bus and its nodes.

  /// Nothing to start on the simulated bus.
  bool EnsureInitialized() noexcept {
    return true;
  }

  /// A frame from the client goes to the nodes.
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    bus.FromClient(frame);
    return true;
  }

  /// The next frame for the client, or a timeout; the bus's clock moves, nothing sleeps.
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    const uint64_t deadline = bus.now_us + timeout_us;
    bus.PumpHeartbeats(deadline);
    if (!bus.rx.empty() && (bus.rx.front().at_us <= deadline)) {
      frame = bus.rx.front().frame;
      bus.now_us = std::max(bus.now_us, bus.rx.front().at_us);
      bus.rx.erase(bus.rx.begin());
      return canopen_client::RxStatus::Frame;
    }
    bus.now_us = deadline;
    return canopen_client::RxStatus::Timeout;
  }

  /// The bus's clock in microseconds.
  [[nodiscard]] uint32_t NowUs() const noexcept {
    return static_cast<uint32_t>(bus.now_us & 0xFFFF'FFFFU);
  }

  /// Driver messages (they carry their own `canopen_client:` prefix) go to the console, one line
  /// each.
  void Log(canopen_client::LogLevel level, const char* message) noexcept {
    static_cast<void>(level);
    static_cast<void>(std::puts(message)); // plain text: never pass it as a format string
  }
};

#endif // CANOPEN_CLIENT_HOST_CANOPEN_CLIENT_PORT_HPP

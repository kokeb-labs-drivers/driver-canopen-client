// clang-format off: the table below must keep one row per line.
/**
 * @file canopen_client_can_interface.hpp
 * @brief The CAN port of the CANopen client driver: the CRTP base and the `CanPort` and
 * `CanRxPort` concepts.
 * @copyright Copyright (c) 2026 The driver-canopen-client authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver reaches the bus through hooks on a class you derive from
 * `canopen_client::CanInterface<Self>`. The controller is configured for classic CAN with 11-bit
 * identifiers at the bit rate of the network (for example 500 kbit/s) by the port; the driver never
 * changes it.
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Start the controller on the first call; `true` at once afterwards |
 * | `bool Send(const CanFrame& frame) noexcept` | Queue one frame; `true` when the controller accepted it |
 * | `RxStatus Receive(CanFrame& frame, uint32_t timeout_us) noexcept` | Wait up to `timeout_us` for one frame (0 polls); `Frame`, `Timeout` or `Error` |
 * | `uint32_t NowUs() noexcept` | A monotonic microsecond counter that wraps at 2^32 |
 *
 * `Send` and `EnsureInitialized` make a transmit-only port (NMT, SYNC, RPDO and unconfirmed SDO
 * writes). `Receive` and `NowUs` come as a pair: with both, the port is a full client port. A port
 * must drop extended-identifier, CAN FD and error frames before `Receive` returns; the driver
 * validates what it gets and counts what it drops.
 *
 * `Send` gets a frame the driver owns: it is valid only during the call, so copy it if you queue it
 * for later. When `Receive` returns `Error` the driver drops what it was waiting for and reports
 * `Bus`; the switch-off calls (`DriveDisable`, `DriveQuickStop`, `SendNmt(Stop)`) still send. After a
 * bus-off the port recovers the controller, and the application calls `Deinitialize()` then
 * `EnsureInitialized()` so the driver starts from a clean state.
 */
// clang-format on
#ifndef CANOPEN_CLIENT_CAN_INTERFACE_HPP
#define CANOPEN_CLIENT_CAN_INTERFACE_HPP

#include <concepts>
#include <cstdint>

#include "canopen_client_detail.hpp"
#include "canopen_client_types.hpp"

namespace canopen_client {

/**
 * @brief CRTP base of the platform CAN port.
 *
 * The base declares no hook itself (a forwarding hook would turn a missing override into infinite
 * recursion); the driver checks your class with the `CanPort` concept.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class CanInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  CanInterface(const CanInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  CanInterface& operator=(const CanInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  CanInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~CanInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `CanInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` (handbook KB-CPP20-006).
 * @tparam Port The port class.
 */
template <typename Port>
concept CanPort = std::derived_from<Port, CanInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(const CanFrame&) noexcept>::Take(&Port::Send);
};

/**
 * @brief Satisfied by a `CanPort` that also receives frames and keeps time: the full client port.
 * @tparam Port The port class.
 */
template <typename Port>
concept CanRxPort = CanPort<Port> && requires {
  detail::ExactHook<Port, RxStatus(CanFrame&, uint32_t) noexcept>::Take(&Port::Receive);
  detail::ExactHook<Port, uint32_t() noexcept>::Take(&Port::NowUs);
};

} // namespace canopen_client

#endif // CANOPEN_CLIENT_CAN_INTERFACE_HPP

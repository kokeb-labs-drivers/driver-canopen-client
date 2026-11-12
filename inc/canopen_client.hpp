/**
 * @file canopen_client.hpp
 * @brief CANopen client driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-canopen-client authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `canopen_client::Client<Port>` is a CANopen master/client over a classic CAN port (see
 * `canopen_client_can_interface.hpp`): NMT commands, an SDO client (expedited and segmented, with
 * abort codes and timeouts), a heartbeat and node-guarding consumer, an EMCY consumer, a SYNC
 * producer, PDO configuration and transfer helpers and a CiA 402 drive state machine helper. No
 * platform code, no heap, no exceptions, no lock.
 */
#ifndef CANOPEN_CLIENT_HPP
#define CANOPEN_CLIENT_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "canopen_client_can_interface.hpp"
#include "canopen_client_detail.hpp"
#include "canopen_client_protocol.hpp"
#include "canopen_client_types.hpp"
#include "canopen_client_version.h"

namespace canopen_client {

namespace detail {

// Optional hook: a `Declares*` concept only tests the name, the `Has*` concept the exact signature,
// `noexcept` and the result type; the driver rejects a hook that is declared but does not match
// (handbook KB-CPP20-007).

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// The port declares something called `Receive`.
template <typename Port>
concept DeclaresReceive = requires(Port& port, CanFrame& frame) { port.Receive(frame, 0_u32); };

/// The port declares something called `NowUs`.
template <typename Port>
concept DeclaresNowUs = requires(Port& port) { port.NowUs(); };

/// Log through the port, or nothing when the port has no `Log()`.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    static_cast<void>(port);
  }
}

/// One node the heartbeat consumer watches.
struct MonitorSlot {
  bool used{false};       ///< The slot holds a node.
  bool toggle{false};     ///< The node-guarding toggle bit expected in the next reply.
  uint8_t node{0};        ///< The node.
  uint32_t timeout_us{0}; ///< How old the last heartbeat may be for the node to count as alive.
  NodeStatus status{};    ///< What was received.
};

/// One subscribed TPDO identifier.
struct PdoSlot {
  bool used{false};   ///< The slot holds an identifier.
  uint8_t dlc{0};     ///< The length every frame on this identifier must have.
  PdoSample sample{}; ///< The latest frame.
};

/// What the receive loop is waiting for.
enum class PendingKind : uint8_t {
  None = 0, ///< Nothing: every frame is routed as unsolicited.
  Sdo,      ///< An SDO reply from `Pending::node`.
  Guard,    ///< A node-guarding reply from `Pending::node`.
  RawSdo,   ///< The first frame from `Pending::node`'s SDO server, taken as it is (`ReadRawSdo()`).
};

/// The one request in flight (the driver runs one at a time).
struct Pending {
  PendingKind kind{PendingKind::None};               ///< What is awaited.
  uint8_t node{0};                                   ///< The node the request went to.
  uint16_t index{0};                                 ///< Object index (SDO).
  uint8_t sub{0};                                    ///< Sub-index (SDO).
  SdoReplyKind expect{SdoReplyKind::InitiateUpload}; ///< The reply kind the next frame must have.
  bool toggle{false};                                ///< The toggle bit a segment reply must carry.
  bool done{false};                                  ///< The wait is over.
  Error error{Error::Ok};                            ///< How it ended.
  uint32_t abort_code{0};           ///< The abort code to send the node when it ended badly.
  SdoReply reply{};                 ///< The accepted SDO reply.
  NmtState state{NmtState::BootUp}; ///< The accepted guarding reply.
};

} // namespace detail

/**
 * @defgroup canopen_client_driver CANopen client
 * @brief The driver class.
 * @{
 */

/**
 * @brief A CANopen client on one CAN bus.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()`, the one way to
 * start, use, optionally `Deinitialize()`. Every call that touches the bus returns
 * `Error::NotInitialized` until `EnsureInitialized()` has succeeded; no call starts it as a side
 * effect.
 *
 * Two kinds of port. A `CanPort` (hooks `EnsureInitialized` and `Send`) gives the transmit-only
 * mode: `SendNmt()`, `SendSync()`, `SendRpdo()` and `SdoDownloadUnconfirmed()`. A `CanRxPort` adds
 * `Receive` and `NowUs` and gives everything else; the calls that need a reply are constrained on
 * it and do not exist on a transmit-only port.
 *
 * One request is in flight at a time. The class is not thread-safe: call it from one task, or
 * serialise the calls.
 *
 * Optional hook: `void Log(LogLevel, const char* message) noexcept`.
 *
 * @tparam Port Your class derived from `CanInterface<Port>`.
 */
template <CanPort Port>
class Client {
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "canopen_client: the optional Log hook must be `void Log(LogLevel, const char* "
                "message) noexcept`");
  static_assert(detail::DeclaresReceive<Port> == detail::DeclaresNowUs<Port>,
                "canopen_client: a port implements both `Receive` and `NowUs`, or neither");
  static_assert(!detail::DeclaresReceive<Port> || CanRxPort<Port>,
                "canopen_client: the hooks must be `RxStatus Receive(CanFrame&, uint32_t "
                "timeout_us) noexcept` and `uint32_t NowUs() noexcept`");

public:
  /**
   * @brief Bind the driver to a port and store the configuration. No I/O, cannot fail.
   * @param port The port; it must outlive the driver.
   * @param config The timeouts and options, checked by `EnsureInitialized()` before any I/O.
   */
  explicit Client(Port& port, const Config& config = {}) noexcept : port_(port), config_(config) {}

  /** @brief One driver object per bus. */
  Client(const Client&) = delete;
  /** @brief One driver object per bus. */
  Client& operator=(const Client&) = delete;

  /**
   * @brief Start the client, once: the only way to initialise the driver.
   *
   * The first call checks the configuration, starts the port and clears the node table, the PDO
   * subscriptions, the EMCY queue and the counters. After it succeeds, later calls return `Ok` at
   * once with no I/O. After it fails, the driver stays uninitialised.
   *
   * @return `Ok`, `InvalidArgument` (the stored configuration fails `Validate()`; the port is not
   * touched) or `Bus` (the port could not start).
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()`. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Mark the driver uninitialised and forget what it received. No bus traffic: nodes are
   * left in the state they are in (send the NMT command you want first).
   * @return `Ok`.
   */
  Result<void> Deinitialize() noexcept;

  /**
   * @brief Replace the configuration.
   * @param config The new configuration.
   * @return `Ok`, or `InvalidArgument` (it fails `Validate()`; the old one stays).
   */
  Result<void> SetConfig(const Config& config) noexcept;

  /** @return The configuration in use. No I/O. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (handbook KB-CPP20-044). */
  void GetConfig() const&& = delete;

  // --- NMT master ---------------------------------------------------------------------------

  /**
   * @brief Send an NMT command. Never refused for a state reason: Stop and every other command go
   * out whatever the driver believes about the node or a transfer in flight. A reset command
   * also restarts the guarding toggle of the node (or of every node).
   * @param command The command.
   * @param node The target, 1 to 127, or 0 for every node.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (node above 127 or a value that is not a
   * command; nothing is sent) or `Bus`.
   */
  Result<void> SendNmt(NmtCommand command, uint8_t node) noexcept;

  // --- SYNC producer, RPDO and unconfirmed SDO (all ports)
  // ------------------------------------------

  /**
   * @brief Send one SYNC frame, with a counter byte when `counter` is not 0.
   * @param counter 0 for a SYNC without data, or 1 to 240.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` or `Bus`.
   */
  Result<void> SendSync(uint8_t counter = 0) noexcept;

  /**
   * @brief Send a PDO the node consumes (an RPDO from the node's side).
   * @param cob_id The identifier, `kPdoCobIdMin` to `kPdoCobIdMax`.
   * @param data 0 to 8 bytes laid out as the node's RPDO mapping says.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` or `Bus`.
   */
  Result<void> SendRpdo(uint16_t cob_id, std::span<const uint8_t> data) noexcept;

  /**
   * @brief Write 1 to 4 bytes to an object with an expedited SDO download and do not wait for the
   * reply: the transmit-only mode's write. The node's answer, including an abort, is not read, so
   * a failed write is not noticed. Use `SdoDownload()` when the port can receive.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param data 1 to 4 bytes, little-endian.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` or `Bus`.
   */
  Result<void> SdoDownloadUnconfirmed(uint8_t node, uint16_t index, uint8_t sub,
                                      std::span<const uint8_t> data) noexcept;

  // --- Raw access (diagnostics) -----------------------------------------------------------------

  /**
   * @brief Diagnostic: send one frame exactly as given. A single `Send()`: no retry, no routing,
   * and none of the driver's state (node table, toggle bits, node states) is touched. It bypasses
   * the safety and state guards: any frame goes out, an NMT command or a controlword included, and
   * the node table does not learn of it. Stop and disable with `SendNmt()`, `DriveDisable()` and
   * `DriveQuickStop()`.
   * @param frame A frame of the port's classic kind: identifier up to 0x7FF, at most 8 bytes.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (the frame fails `IsValid()`; nothing is
   * sent) or `Bus`.
   */
  Result<void> SendRawFrame(const CanFrame& frame) noexcept;

  /**
   * @brief Diagnostic: take the next frame from the port as it is. One `Receive()`: no retry, no
   * routing, no validation beyond the port's framing; the node table, the EMCY queue and the
   * TPDO samples do not see the frame. The frame is also kept for `GetLastRawFrame()`.
   * @param timeout_us How long to wait; 0 polls once.
   * @return The frame, `NotInitialized`, `Timeout` or `Bus`.
   */
  Result<CanFrame> ReadRawFrame(uint32_t timeout_us) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Diagnostic: one SDO initiate-upload request and the first frame the node's SDO server
   * (identifier 0x580 + node) answers with, returned as received. One request, no retry, no
   * segment follow-up, no toggle, size or multiplexer check, no node-state gate and no abort on a
   * timeout. The receive queue is drained first (routed as usual), so the frame returned arrived
   * after the request. Other frames that arrive meanwhile are routed as usual. Use it to see what a
   * node really sends; use `SdoUpload()` to read an object.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @return The reply frame, `NotInitialized`, `InvalidArgument` (nothing is sent), `Timeout`
   * (`Config::sdo_timeout_us`) or `Bus`.
   */
  Result<CanFrame> ReadRawSdo(uint8_t node, uint16_t index, uint8_t sub) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Diagnostic: one expedited SDO download request of 1 to 4 bytes and the first frame
   * the node's SDO server answers with, returned as received (a download response or an abort).
   * The same rules as `ReadRawSdo()`.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param data 1 to 4 bytes, little-endian.
   * @return The reply frame, `NotInitialized`, `InvalidArgument`, `Timeout` or `Bus`.
   */
  Result<CanFrame> WriteRawSdo(uint8_t node, uint16_t index, uint8_t sub,
                               std::span<const uint8_t> data) noexcept
    requires CanRxPort<Port>;

  /** @return The last frame `ReadRawFrame()`, `ReadRawSdo()` or `WriteRawSdo()` returned (no I/O);
   * all zero before the first. */
  [[nodiscard]] const CanFrame& GetLastRawFrame() const& noexcept {
    return last_raw_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (handbook KB-CPP20-044). */
  void GetLastRawFrame() const&& = delete;

  // --- Receive path -----------------------------------------------------------------------------

  /**
   * @brief Wait up to `timeout_us` for one frame and route it: heartbeat to the node table, EMCY to
   * the queue, a subscribed TPDO to its sample, SYNC counted. A frame nobody asked for
   * (including an SDO reply with no request in flight) is dropped and counted.
   * @param timeout_us How long to wait; 0 polls once.
   * @return What the frame was (`FrameClass::None` when none arrived), `NotInitialized` or `Bus`.
   */
  Result<FrameClass> Poll(uint32_t timeout_us) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Watch a node's heartbeat. Registering again changes the timeout and keeps what was
   * received.
   * @param node The node, 1 to 127.
   * @param timeout_us How old the last heartbeat may be for `alive`, 1 ms to 60 s.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, or `BufferTooSmall` (the table, of
   * `kMonitorCapacity` nodes, is full).
   */
  Result<void> MonitorNode(uint8_t node, uint32_t timeout_us) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Stop watching a node.
   * @param node The node.
   * @return `Ok`, `NotInitialized` or `InvalidArgument` (not watched).
   */
  Result<void> UnmonitorNode(uint8_t node) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief What the consumer knows about a watched node; `alive` is judged now. The age is a 32-bit
   * microsecond difference, so a heartbeat older than 71.6 minutes can look fresh: call this or
   * `Poll()` at least hourly.
   * @param node The node.
   * @return The status, `NotInitialized` or `InvalidArgument` (not watched).
   */
  Result<NodeStatus> GetNodeStatus(uint8_t node) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Forget that a boot-up message was seen from a node (`NodeStatus::bootup_seen`), so the
   * next one (the node reset on its own) can be told from the first. The flag is otherwise set
   * once and cleared only by an NMT reset this driver sends.
   * @param node A node registered with `MonitorNode()`.
   * @return `Ok`, `NotInitialized` or `InvalidArgument` (not watched).
   */
  Result<void> ClearBootUp(uint8_t node) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief One node-guarding exchange: a remote frame on 0x700 + node, then the reply, whose
   * toggle bit must alternate (the first reply after a reset has it clear). A reply with the
   * wrong toggle fails and resynchronises the expected bit to the one after it.
   * @param node A node registered with `MonitorNode()`.
   * @return The reported state, `NotInitialized`, `InvalidArgument`, `Timeout` (no reply within
   * `Config::guard_timeout_us`), `Protocol` (toggle not alternated) or `Bus`.
   */
  Result<NmtState> GuardNode(uint8_t node) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Take the oldest queued emergency message.
   * @param[out] event The message; unchanged when the queue is empty.
   * @return true when one was returned.
   */
  bool PopEmcy(EmcyEvent& event) noexcept
    requires CanRxPort<Port>;

  /** @return The receive counters. No I/O. */
  [[nodiscard]] const Stats& GetStats() const& noexcept {
    return stats_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (handbook KB-CPP20-044). */
  void GetStats() const&& = delete;

  /**
   * @brief Hold the latest frame of a TPDO identifier for `ReadTpdo()`.
   * @param cob_id The identifier, `kPdoCobIdMin` to `kPdoCobIdMax`.
   * @param dlc The length every frame on it must have, 1 to 8; others are dropped and counted.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, or `BufferTooSmall` (the table, of
   * `kPdoSubscriptionCapacity` identifiers, is full).
   */
  Result<void> SubscribeTpdo(uint16_t cob_id, uint8_t dlc) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Stop holding a TPDO identifier.
   * @param cob_id The identifier.
   * @return `Ok`, `NotInitialized` or `InvalidArgument` (not subscribed).
   */
  Result<void> UnsubscribeTpdo(uint16_t cob_id) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief The latest frame received on a subscribed identifier.
   * @param cob_id The identifier.
   * @return The sample (`sequence` 0 means no frame yet), `NotInitialized` or `InvalidArgument`.
   */
  Result<PdoSample> ReadTpdo(uint16_t cob_id) const noexcept
    requires CanRxPort<Port>;

  // --- SDO client -----------------------------------------------------------------------------

  /**
   * @brief Read an object: expedited when the node answers so, otherwise segmented. A reply that
   * is not the answer to this request (another object, another command, a wrong toggle bit,
   * the wrong length) never counts; see the Contract page.
   *
   * The node is refused with `NodeState` without a frame when its heartbeat says it is Stopped.
   *
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param out Where the data goes.
   * @return The number of bytes read, or `NotInitialized`, `InvalidArgument`, `NodeState`, `Bus`,
   * `Timeout` (the driver then sends an SDO abort when `Config::abort_on_timeout`), `SdoAbort`
   * (the node aborted: `GetLastAbort()`), `Protocol`, or `BufferTooSmall` (the driver aborts the
   * transfer with 0x05040005; no data is written past `out`).
   */
  Result<std::size_t> SdoUpload(uint8_t node, uint16_t index, uint8_t sub,
                                std::span<uint8_t> out) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Write an object: expedited for 1 to 4 bytes, segmented for more.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param data 1 byte or more (at most 2^32 - 1), little-endian.
   * @return `Ok`, or the errors of `SdoUpload()` except `BufferTooSmall`.
   */
  Result<void> SdoDownload(uint8_t node, uint16_t index, uint8_t sub,
                           std::span<const uint8_t> data) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Read an unsigned 8-bit object.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @return The value, or the errors of `SdoUpload()`; `Protocol` when the node returns another
   * length than 1.
   */
  Result<uint8_t> SdoReadU8(uint8_t node, uint16_t index, uint8_t sub) noexcept
    requires CanRxPort<Port>;
  /**
   * @brief Read an unsigned 16-bit object.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @return The value, or the errors of `SdoUpload()`; `Protocol` when the node returns another
   * length than 2.
   */
  Result<uint16_t> SdoReadU16(uint8_t node, uint16_t index, uint8_t sub) noexcept
    requires CanRxPort<Port>;
  /**
   * @brief Read an unsigned 32-bit object.
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @return The value, or the errors of `SdoUpload()`; `Protocol` when the node returns another
   * length than 4.
   */
  Result<uint32_t> SdoReadU32(uint8_t node, uint16_t index, uint8_t sub) noexcept
    requires CanRxPort<Port>;
  /**
   * @brief Write an unsigned 8-bit object (expedited, one byte).
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param value The value.
   * @return `Ok` or the errors of `SdoDownload()`.
   */
  Result<void> SdoWriteU8(uint8_t node, uint16_t index, uint8_t sub, uint8_t value) noexcept
    requires CanRxPort<Port>;
  /**
   * @brief Write an unsigned 16-bit object (expedited, two bytes).
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param value The value.
   * @return `Ok` or the errors of `SdoDownload()`.
   */
  Result<void> SdoWriteU16(uint8_t node, uint16_t index, uint8_t sub, uint16_t value) noexcept
    requires CanRxPort<Port>;
  /**
   * @brief Write an unsigned 32-bit object (expedited, four bytes).
   * @param node The server, 1 to 127.
   * @param index The object index.
   * @param sub The sub-index.
   * @param value The value.
   * @return `Ok` or the errors of `SdoDownload()`.
   */
  Result<void> SdoWriteU32(uint8_t node, uint16_t index, uint8_t sub, uint32_t value) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief The last SDO abort, sent by a node or by this driver. No I/O.
   * @return The abort; `code` is 0 when no transfer has aborted since `EnsureInitialized()`.
   */
  [[nodiscard]] const SdoAbortInfo& GetLastAbort() const& noexcept {
    return last_abort_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (handbook KB-CPP20-044). */
  void GetLastAbort() const&& = delete;

  // --- Communication objects of a node
  // ------------------------------------------------------------

  /**
   * @brief Read the device type (object 0x1000).
   * @param node The server, 1 to 127.
   * @return The value or the errors of `SdoReadU32()`.
   */
  Result<uint32_t> ReadDeviceType(uint8_t node) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Read the error register (object 0x1001).
   * @param node The server, 1 to 127.
   * @return The value or the errors of `SdoReadU8()`.
   */
  Result<uint8_t> ReadErrorRegister(uint8_t node) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Set the node's heartbeat producer time (object 0x1017).
   * @param node The server, 1 to 127.
   * @param period_ms The period in milliseconds; 0 stops the heartbeat.
   * @return `Ok` or the errors of `SdoWriteU16()`.
   */
  Result<void> WriteHeartbeatProducerTime(uint8_t node, uint16_t period_ms) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Store parameters to non-volatile memory (object 0x1010, signature "save").
   * @param node The server, 1 to 127.
   * @param group The sub-index: 1 all, 2 communication, 3 application, 4 manufacturer.
   * @return `Ok`, `InvalidArgument` (group not 1 to 4) or the errors of `SdoWriteU32()`.
   */
  Result<void> StoreParameters(uint8_t node, uint8_t group) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Restore default parameters (object 0x1011, signature "load"); they apply after the
   * next reset of the node.
   * @param node The server, 1 to 127.
   * @param group The sub-index: 1 all, 2 communication, 3 application, 4 manufacturer.
   * @return `Ok`, `InvalidArgument` (group not 1 to 4) or the errors of `SdoWriteU32()`.
   */
  Result<void> RestoreDefaults(uint8_t node, uint8_t group) noexcept
    requires CanRxPort<Port>;

  // --- SYNC producer with a period -------------------------------------------------------------

  /**
   * @brief Set the SYNC period `ServiceSync()` keeps.
   * @param period_us The period, 100 us to 60 s, or 0 to stop.
   * @param counter_overflow 0 for no counter byte, or 2 to 240: the counter counts 1 up to it, then
   * wraps to 1.
   * @return `Ok`, `NotInitialized` or `InvalidArgument`.
   */
  Result<void> ConfigureSync(uint32_t period_us, uint8_t counter_overflow) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Send a SYNC when the period has elapsed since the last one (the first call after
   * `ConfigureSync()` sends at once). Call it at least as often as the period; a late call sends
   * one SYNC and starts the next period from now, it never sends a burst to catch up.
   * @return true when a SYNC was sent, false when it is not due or stopped; `NotInitialized` or
   * `Bus`.
   */
  Result<bool> ServiceSync() noexcept
    requires CanRxPort<Port>;

  // --- PDO configuration ------------------------------------------------------------------------

  /**
   * @brief Configure a PDO of a node over SDO, in the order the profile prescribes: invalidate the
   * COB-ID, write the transmission type (and for a TPDO the inhibit time and event timer when not
   * 0), clear the mapping count, write each entry, write the count, then write the COB-ID valid
   * (when `config.enabled`). A failure leaves the PDO invalid, never half-configured and valid.
   * @param node The server, 1 to 127.
   * @param kind Receive (RPDO) or transmit (TPDO) from the node's side.
   * @param number The PDO number, 1 to 512.
   * @param config The parameters; it must pass `ValidatePdoConfig()` and map at least one entry.
   * @return `Ok`, `InvalidArgument` (nothing is sent) or the errors of `SdoWriteU32()`.
   */
  Result<void> ConfigurePdo(uint8_t node, PdoKind kind, uint16_t number,
                            const PdoConfig& config) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Invalidate a PDO (set bit 31 of its COB-ID, keeping the identifier).
   * @param node The server, 1 to 127.
   * @param kind Receive or transmit.
   * @param number The PDO number, 1 to 512.
   * @return `Ok` or the errors of `SdoReadU32()` and `SdoWriteU32()`.
   */
  Result<void> DisablePdo(uint8_t node, PdoKind kind, uint16_t number) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Read a PDO's identifier, validity, transmission type and mapping back.
   * @param node The server, 1 to 127.
   * @param kind Receive or transmit.
   * @param number The PDO number, 1 to 512.
   * @return The configuration (the inhibit time and event timer are not read), `Unsupported` for a
   * 29-bit identifier, `BufferTooSmall` for a mapping with more than `kMaxPdoEntries` entries, or
   * the SDO errors.
   */
  Result<PdoConfig> ReadPdoConfig(uint8_t node, PdoKind kind, uint16_t number) noexcept
    requires CanRxPort<Port>;

  // --- CiA 402 drive
  // -------------------------------------------------------------------------------

  /**
   * @brief Read the statusword.
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return The value or the errors of `SdoReadU16()`; `InvalidArgument` for an index below 0x1000.
   */
  Result<uint16_t> ReadStatusword(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Read the statusword and decode the drive state.
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return The state (`Unknown` for a pattern that is none), or the errors of `ReadStatusword()`.
   */
  Result<DriveState> ReadDriveState(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Walk the drive to Operation enabled: shutdown, switch on, enable operation, reading the
   * statusword after each controlword and pausing `Config::drive_poll_interval_us`. Each state gets
   * one controlword, so a slow drive's transition is never restarted. From Quick stop active it
   * first disables voltage. It does not clear a fault.
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return `Ok`, `DriveFault` (Fault, or a statusword that is no state), `Timeout`
   * (`Config::drive_timeout_us` passed without reaching it: the last controlword written may still
   * take effect, so follow a `Timeout` with `DriveDisable()` if the drive must not run) or the SDO
   * errors.
   */
  Result<void> DriveEnable(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Disable voltage (controlword 0x0000), from any state. Never refused: it ignores the
   * node's heartbeat state and any earlier failure, including a `Receive()` error (the receive
   * queue is dropped and the request is sent anyway), and is attempted `Config::safety_attempts`
   * times until the node acknowledges it. A port that cannot send, or a node that never answers,
   * still ends in an error: the attempts are bounded.
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, or the error of the last attempt (`Bus` when
   * the port's `Receive()` failed, after the frame was sent).
   */
  Result<void> DriveDisable(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Quick stop (controlword 0x0002). Never refused, retried like `DriveDisable()`.
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return As `DriveDisable()`.
   */
  Result<void> DriveQuickStop(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Reset a fault: wait out Fault reaction active, then give controlword bit 7 a rising
   * edge and wait for the drive to leave Fault. A drive that is not in a fault state is left as
   * it is; one whose statusword is no state is `DriveFault`. Both waits together take at most
   * `Config::drive_timeout_us`. On success the controlword is 0x0000 again (bit 7 low).
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return `Ok`, `DriveFault` (the fault persists), `Timeout` or the SDO errors.
   */
  Result<void> DriveResetFault(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Select the mode of operation.
   * @param node The server, 1 to 127.
   * @param mode The mode; a manufacturer-specific negative value is a cast of the number.
   * @param objects The drive's object indices.
   * @param verify `Verify::ReadBack` reads the mode display until it shows `mode`, for at most
   * `Config::drive_timeout_us` (one extra SDO exchange per poll); the default does not.
   * @return `Ok`, `DeviceFault` (the display never showed the mode) or the SDO errors.
   */
  Result<void> SetDriveMode(uint8_t node, OperationMode mode, const DriveObjects& objects = {},
                            Verify verify = Verify::None) noexcept
    requires CanRxPort<Port>;

  /**
   * @brief Read the mode of operation display.
   * @param node The server, 1 to 127.
   * @param objects The drive's object indices.
   * @return The mode, or the errors of `SdoReadU8()`.
   */
  Result<OperationMode> ReadDriveMode(uint8_t node, const DriveObjects& objects = {}) noexcept
    requires CanRxPort<Port>;

private:
  // Common
  Result<void> sendFrame(const CanFrame& frame) noexcept;
  void resetState() noexcept;
  void noteNmt(NmtCommand command, uint8_t node) noexcept;
  // Routing of received frames
  FrameClass route(const CanFrame& frame) noexcept
    requires CanRxPort<Port>;
  FrameClass routeHeartbeat(const CanFrame& frame) noexcept
    requires CanRxPort<Port>;
  FrameClass routeEmcy(const CanFrame& frame) noexcept
    requires CanRxPort<Port>;
  FrameClass routePdo(const CanFrame& frame) noexcept
    requires CanRxPort<Port>;
  void onSdoReply(const CanFrame& frame) noexcept
    requires CanRxPort<Port>;
  void onGuardReply(const CanFrame& frame) noexcept
    requires CanRxPort<Port>;
  void finish(Error error, uint32_t abort_code) noexcept;
  void clearPending() noexcept;
  Result<void> awaitPending(uint32_t timeout_us) noexcept
    requires CanRxPort<Port>;
  Result<void> drainRx() noexcept
    requires CanRxPort<Port>;
  Result<void> idleFor(uint32_t us) noexcept
    requires CanRxPort<Port>;
  [[nodiscard]] detail::MonitorSlot* findSlot(uint8_t node) noexcept;
  // SDO engine
  Result<void> sdoBegin(uint8_t node, uint16_t index, uint8_t sub, bool gated) noexcept
    requires CanRxPort<Port>;
  Result<void> sdoExchange(const CanFrame& request, SdoReplyKind expect, bool toggle) noexcept
    requires CanRxPort<Port>;
  void abortLocal(uint32_t code) noexcept;
  Result<std::size_t> uploadSegments(std::span<uint8_t> out) noexcept
    requires CanRxPort<Port>;
  Result<bool> uploadSegment(std::span<uint8_t> out, std::size_t& got, bool toggle) noexcept
    requires CanRxPort<Port>;
  Result<void> downloadSegments(std::span<const uint8_t> data) noexcept
    requires CanRxPort<Port>;
  Result<uint32_t> readUnsigned(uint8_t node, uint16_t index, uint8_t sub,
                                std::size_t size) noexcept
    requires CanRxPort<Port>;
  Result<void> writeUnsigned(uint8_t node, uint16_t index, uint8_t sub, uint32_t value,
                             std::size_t size, bool gated) noexcept
    requires CanRxPort<Port>;
  Result<void> storeOrRestore(uint8_t node, uint16_t index, uint8_t group,
                              uint32_t signature) noexcept
    requires CanRxPort<Port>;
  // PDO and drive
  Result<void> writePdoMapping(uint8_t node, uint16_t map_index, const PdoConfig& config) noexcept
    requires CanRxPort<Port>;
  Result<void> writePdoCommunication(uint8_t node, PdoKind kind, uint16_t comm_index,
                                     const PdoConfig& config) noexcept
    requires CanRxPort<Port>;
  Result<void> safetyWriteControlword(uint8_t node, const DriveObjects& objects,
                                      uint16_t word) noexcept
    requires CanRxPort<Port>;
  Result<void> waitFaultCleared(uint8_t node, const DriveObjects& objects, uint32_t start) noexcept
    requires CanRxPort<Port>;
  [[nodiscard]] bool driveTimedOut(uint32_t start_us) noexcept
    requires CanRxPort<Port>;
  [[nodiscard]] uint32_t driveStepLimit() const noexcept;
  Result<CanFrame> rawExchange(uint8_t node, const CanFrame& request) noexcept
    requires CanRxPort<Port>;

  Port& port_;
  Config config_{};
  bool initialized_{false};
  // Transfer state lives in the object, not on the stack: a port may keep the pointer it is given
  // until the frame has left the controller, so `Send()` always gets `tx_`, never a local.
  CanFrame tx_{};             ///< The frame being sent.
  CanFrame last_raw_{};       ///< The last raw frame returned.
  CanFrame rx_{};             ///< The frame being received.
  detail::Pending pending_{}; ///< The request in flight.
  SdoAbortInfo last_abort_{}; ///< The last abort.
  Stats stats_{};             ///< Receive counters.
  std::array<detail::MonitorSlot, kMonitorCapacity> monitors_{};
  std::array<detail::PdoSlot, kPdoSubscriptionCapacity> pdos_{};
  std::array<EmcyEvent, kEmcyQueueCapacity> emcy_{}; ///< Ring buffer of queued events.
  std::size_t emcy_head_{0};                         ///< Index of the oldest queued event.
  std::size_t emcy_count_{0};                        ///< Queued events.
  uint32_t sync_period_us_{0};                       ///< 0 when stopped.
  uint8_t sync_overflow_{0};                         ///< Counter overflow, 0 for none.
  uint8_t sync_counter_{0};                          ///< The counter sent last.
  bool sync_started_{false};                         ///< A SYNC went out since `ConfigureSync()`.
  uint32_t sync_last_us_{0};                         ///< `NowUs()` at the last SYNC.
};

/** @} */

} // namespace canopen_client

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/canopen_client.ipp" // IWYU pragma: export

#endif // CANOPEN_CLIENT_HPP

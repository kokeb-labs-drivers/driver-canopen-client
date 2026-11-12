/**
 * @file canopen_client_types.hpp
 * @brief Public types of the CANopen client driver: errors, `Result<T>`, the CAN frame, the
 * protocol vocabulary (NMT, SDO, EMCY, PDO, drive states) and the configuration.
 * @copyright Copyright (c) 2026 The driver-canopen-client authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver speaks classic CAN with 11-bit identifiers. Everything here is a plain value type; the
 * frame codecs are in `canopen_client_protocol.hpp` and the client class in `canopen_client.hpp`.
 */
#ifndef CANOPEN_CLIENT_TYPES_HPP
#define CANOPEN_CLIENT_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "canopen_client_version.h"

/** @brief The CANopen client driver: everything it declares lives here; internals in `detail`. */
namespace canopen_client {

/**
 * @defgroup canopen_client_types Driver types
 * @brief Errors, results, frames, protocol vocabulary and configuration.
 * @{
 */

/**
 * @brief Why a call failed. Values 0 to 9 are the shared base set; client-specific values start
 * at 16.
 *
 * `DeviceIdMismatch`, `Crc` and `Busy` are never returned: the CAN controller checks the frame
 * CRC, and every call blocks until it finishes.
 */
enum class [[nodiscard]] Error : uint8_t {
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran since.
  InvalidArgument = 2,  ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,              ///< The port failed to send or to receive a frame.
  Timeout = 4,          ///< No valid reply arrived in time (SDO, node guarding, drive state).
  DeviceIdMismatch = 5, ///< Not returned by this driver.
  Crc = 6,              ///< Not returned by this driver.
  Busy = 7,             ///< Not returned by this driver.
  DeviceFault = 8,      ///< A read-back showed a value other than the one written.
  Unsupported = 9,      ///< The call needs a port hook the port lacks, or a transfer type the
                        ///< driver does not implement.
  SdoAbort = 16,        ///< The node aborted the SDO transfer; see `GetLastAbort()`.
  Protocol = 17,        ///< A reply broke the protocol (command specifier, toggle bit, size).
  BufferTooSmall = 18,  ///< An upload is larger than the caller's buffer.
  InvalidFrame = 19,    ///< A frame fails validation (identifier, length or content).
  NodeState = 20,       ///< The node is known to be in an NMT state that ignores the request.
  DriveFault = 21, ///< The drive is in the Fault state or reports a state no transition leaves.
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::SdoAbort:
    return "SdoAbort";
  case Error::Protocol:
    return "Protocol";
  case Error::BufferTooSmall:
    return "BufferTooSmall";
  case Error::InvalidFrame:
    return "InvalidFrame";
  case Error::NodeState:
    return "NodeState";
  case Error::DriveFault:
    return "DriveFault";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }
  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }
  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events.
  Debug,     ///< Detail for bring-up.
};

/** @brief Whether a setter or a transfer reads back what it wrote (extra bus traffic). */
enum class Verify : uint8_t {
  None = 0, ///< No read-back: the node's acknowledgement is the only check.
  ReadBack, ///< Read the object again and compare; costs one more SDO exchange.
};

// --- The CAN frame ---------------------------------------------------------------------------

/// Largest 11-bit CAN identifier.
inline constexpr uint16_t kMaxCanId = 0x7FF;
/// Longest classic CAN payload in bytes.
inline constexpr uint8_t kMaxDlc = 8;

/**
 * @brief One classic CAN frame with an 11-bit identifier.
 *
 * Extended (29-bit) and CAN FD frames are not representable: a port drops them. Data bytes past
 * `dlc` are zero in every frame the driver builds and are ignored in every frame it receives.
 */
struct CanFrame {
  uint16_t id{0};                ///< The 11-bit identifier (COB-ID).
  uint8_t dlc{0};                ///< Number of data bytes, 0 to 8.
  bool rtr{false};               ///< Remote transmission request (used by node guarding only).
  std::array<uint8_t, 8> data{}; ///< The payload; bytes at and past `dlc` are not meaningful.
};

/**
 * @brief Whether a frame is representable on the bus: identifier at most 0x7FF, length at most 8.
 * @param frame The frame.
 * @return true when valid.
 */
constexpr bool IsValid(const CanFrame& frame) noexcept {
  return (frame.id <= kMaxCanId) && (frame.dlc <= kMaxDlc);
}

/** @brief What a port's `Receive()` hook reports. */
enum class RxStatus : uint8_t {
  Frame = 0, ///< A frame was received into the argument.
  Timeout,   ///< No frame arrived within the timeout (not an error).
  Error,     ///< The controller failed (bus off, driver not running).
};

// --- Identifiers -----------------------------------------------------------------------------

/// Node identifier of "every node" in an NMT command.
inline constexpr uint8_t kBroadcastNode = 0;
/// Lowest valid node identifier.
inline constexpr uint8_t kMinNodeId = 1;
/// Highest valid node identifier.
inline constexpr uint8_t kMaxNodeId = 127;

/// NMT command identifier (pre-defined connection set).
inline constexpr uint16_t kCobIdNmt = 0x000;
/// SYNC identifier.
inline constexpr uint16_t kCobIdSync = 0x080;
/// EMCY identifiers are this plus the node identifier.
inline constexpr uint16_t kCobIdEmcyBase = 0x080;
/// SDO response (server to client) identifiers are this plus the node identifier.
inline constexpr uint16_t kCobIdSdoResponseBase = 0x580;
/// SDO request (client to server) identifiers are this plus the node identifier.
inline constexpr uint16_t kCobIdSdoRequestBase = 0x600;
/// Heartbeat and node-guarding identifiers are this plus the node identifier.
inline constexpr uint16_t kCobIdHeartbeatBase = 0x700;
/// Lowest identifier the driver accepts for a PDO (the first pre-defined TPDO1 of node 1).
inline constexpr uint16_t kPdoCobIdMin = 0x181;
/// Highest identifier the driver accepts for a PDO (the last pre-defined RPDO4 identifier).
inline constexpr uint16_t kPdoCobIdMax = 0x57F;

// --- Capacities (fixed, no heap) -----------------------------------------------------------------

/// Nodes the heartbeat and guarding consumer can watch at once.
inline constexpr std::size_t kMonitorCapacity = 16;
/// TPDO identifiers the receive path can hold the latest frame of.
inline constexpr std::size_t kPdoSubscriptionCapacity = 16;
/// EMCY events queued between two `PopEmcy()` calls.
inline constexpr std::size_t kEmcyQueueCapacity = 8;
/// Entries of one PDO mapping the helpers handle.
inline constexpr std::size_t kMaxPdoEntries = 16;
/// Frames one blocking wait looks at before it gives up (a flooded bus must not stall a call).
inline constexpr uint32_t kMaxFramesPerWait = 256;
/// Frames drained and routed before a request is sent.
inline constexpr uint32_t kMaxDrainFrames = 64;
/// Segments of one segmented SDO transfer (7 bytes each).
inline constexpr uint32_t kMaxSdoSegments = 65536;
/// `Receive()` calls one blocking wait makes at most, whatever they return, before it gives up.
/// Only frames count against `kMaxFramesPerWait`; this bounds a port that returns early with no
/// frame.
inline constexpr uint32_t kMaxReceivesPerWait = 65536;

// --- NMT ---------------------------------------------------------------------------------------

/** @brief NMT master command (the first data byte of the frame on identifier 0x000). */
enum class NmtCommand : uint8_t {
  Start = 0x01,              ///< Go to Operational.
  Stop = 0x02,               ///< Go to Stopped.
  PreOperational = 0x80,     ///< Go to Pre-operational.
  ResetNode = 0x81,          ///< Reset the whole application and communication.
  ResetCommunication = 0x82, ///< Reset the communication parameters only.
};

/** @brief NMT state a node reports in its heartbeat or guarding reply (bit 7 masked off). */
enum class NmtState : uint8_t {
  BootUp = 0x00,         ///< The boot-up message; the node continues to Pre-operational.
  Stopped = 0x04,        ///< Only NMT and heartbeat; SDO and PDO are off.
  Operational = 0x05,    ///< SDO and PDO are on.
  PreOperational = 0x7F, ///< SDO is on, PDO is off.
};

// --- SDO ---------------------------------------------------------------------------------------

/**
 * @brief SDO abort codes the driver names. Any 32-bit value can arrive; `SdoAbortInfo::code` keeps
 * the raw value and `AbortCodeName()` names it.
 */
enum class SdoAbortCode : uint32_t {
  ToggleNotAlternated = 0x05030000, ///< Toggle bit not alternated.
  Timeout = 0x05040000,             ///< SDO protocol timed out.
  InvalidCommandSpecifier =
      0x05040001,                     ///< Client or server command specifier not valid or unknown.
  InvalidBlockSize = 0x05040002,      ///< Invalid block size (block mode only).
  InvalidSequenceNumber = 0x05040003, ///< Invalid sequence number (block mode only).
  CrcError = 0x05040004,              ///< CRC error (block mode only).
  OutOfMemory = 0x05040005,           ///< Out of memory.
  UnsupportedAccess = 0x06010000,     ///< Unsupported access to an object.
  WriteOnly = 0x06010001,             ///< Attempt to read a write-only object.
  ReadOnly = 0x06010002,              ///< Attempt to write a read-only object.
  ObjectNotFound = 0x06020000,        ///< Object does not exist in the object dictionary.
  NotMappable = 0x06040041,           ///< Object cannot be mapped to the PDO.
  PdoLengthExceeded = 0x06040042,     ///< The mapped objects would exceed the PDO length.
  ParameterIncompatible = 0x06040043, ///< General parameter incompatibility.
  InternalIncompatibility = 0x06040047, ///< General internal incompatibility in the device.
  HardwareError = 0x06060000,           ///< Access failed due to a hardware error.
  LengthMismatch =
      0x06070010, ///< Data type does not match, length of service parameter does not match.
  LengthTooHigh = 0x06070012, ///< Data type does not match, length of service parameter too high.
  LengthTooLow = 0x06070013,  ///< Data type does not match, length of service parameter too low.
  SubIndexNotFound = 0x06090011,       ///< Sub-index does not exist.
  InvalidValue = 0x06090030,           ///< Invalid value for parameter (download only).
  ValueTooHigh = 0x06090031,           ///< Value of parameter written too high (download only).
  ValueTooLow = 0x06090032,            ///< Value of parameter written too low (download only).
  MaxLessThanMin = 0x06090036,         ///< Maximum value is less than minimum value.
  SdoResourceUnavailable = 0x060A0023, ///< Resource not available: SDO connection.
  GeneralError = 0x08000000,           ///< General error.
  StoreFailed = 0x08000020,            ///< Data cannot be transferred or stored to the application.
  LocalControl = 0x08000021,           ///< ... because of local control.
  DeviceState = 0x08000022,            ///< ... because of the present device state.
  NoDictionary = 0x08000023, ///< Object dictionary dynamic generation fails or none present.
  NoData = 0x08000024,       ///< No data available.
};

namespace detail {

/// One named abort code.
struct AbortName {
  SdoAbortCode code;
  const char* name;
};

/// Every abort code the driver names, for `AbortCodeName()`.
inline constexpr std::array<AbortName, 31> kAbortNames{{
    {.code = SdoAbortCode::ToggleNotAlternated, .name = "ToggleNotAlternated"},
    {.code = SdoAbortCode::Timeout, .name = "Timeout"},
    {.code = SdoAbortCode::InvalidCommandSpecifier, .name = "InvalidCommandSpecifier"},
    {.code = SdoAbortCode::InvalidBlockSize, .name = "InvalidBlockSize"},
    {.code = SdoAbortCode::InvalidSequenceNumber, .name = "InvalidSequenceNumber"},
    {.code = SdoAbortCode::CrcError, .name = "CrcError"},
    {.code = SdoAbortCode::OutOfMemory, .name = "OutOfMemory"},
    {.code = SdoAbortCode::UnsupportedAccess, .name = "UnsupportedAccess"},
    {.code = SdoAbortCode::WriteOnly, .name = "WriteOnly"},
    {.code = SdoAbortCode::ReadOnly, .name = "ReadOnly"},
    {.code = SdoAbortCode::ObjectNotFound, .name = "ObjectNotFound"},
    {.code = SdoAbortCode::NotMappable, .name = "NotMappable"},
    {.code = SdoAbortCode::PdoLengthExceeded, .name = "PdoLengthExceeded"},
    {.code = SdoAbortCode::ParameterIncompatible, .name = "ParameterIncompatible"},
    {.code = SdoAbortCode::InternalIncompatibility, .name = "InternalIncompatibility"},
    {.code = SdoAbortCode::HardwareError, .name = "HardwareError"},
    {.code = SdoAbortCode::LengthMismatch, .name = "LengthMismatch"},
    {.code = SdoAbortCode::LengthTooHigh, .name = "LengthTooHigh"},
    {.code = SdoAbortCode::LengthTooLow, .name = "LengthTooLow"},
    {.code = SdoAbortCode::SubIndexNotFound, .name = "SubIndexNotFound"},
    {.code = SdoAbortCode::InvalidValue, .name = "InvalidValue"},
    {.code = SdoAbortCode::ValueTooHigh, .name = "ValueTooHigh"},
    {.code = SdoAbortCode::ValueTooLow, .name = "ValueTooLow"},
    {.code = SdoAbortCode::MaxLessThanMin, .name = "MaxLessThanMin"},
    {.code = SdoAbortCode::SdoResourceUnavailable, .name = "SdoResourceUnavailable"},
    {.code = SdoAbortCode::GeneralError, .name = "GeneralError"},
    {.code = SdoAbortCode::StoreFailed, .name = "StoreFailed"},
    {.code = SdoAbortCode::LocalControl, .name = "LocalControl"},
    {.code = SdoAbortCode::DeviceState, .name = "DeviceState"},
    {.code = SdoAbortCode::NoDictionary, .name = "NoDictionary"},
    {.code = SdoAbortCode::NoData, .name = "NoData"},
}};

} // namespace detail

/**
 * @brief The name of an abort code, for logs.
 * @param code The raw 32-bit abort code.
 * @return A static string; `"Unknown"` for a code the driver does not name.
 */
constexpr const char* AbortCodeName(uint32_t code) noexcept {
  for (const detail::AbortName& entry : detail::kAbortNames) {
    if (static_cast<uint32_t>(entry.code) == code) {
      return entry.name;
    }
  }
  return "Unknown";
}

/** @brief The last SDO abort, from the node or from this driver. */
struct SdoAbortInfo {
  uint8_t node{0};    ///< The node of the transfer.
  uint16_t index{0};  ///< The object index of the transfer.
  uint8_t sub{0};     ///< The sub-index of the transfer.
  uint32_t code{0};   ///< The raw abort code; 0 when no transfer has aborted.
  bool remote{false}; ///< true when the node sent it, false when this driver aborted the transfer.
};

/** @brief The kind of an SDO server reply. */
enum class SdoReplyKind : uint8_t {
  InitiateDownload = 0, ///< Response to an initiate download request (scs 3).
  SegmentDownload,      ///< Response to a download segment (scs 1).
  InitiateUpload,       ///< Response to an initiate upload request (scs 2).
  SegmentUpload,        ///< A segment of upload data (scs 0).
  Abort,                ///< Abort transfer (scs 4).
};

/** @brief A decoded SDO server reply. */
struct SdoReply {
  SdoReplyKind kind{SdoReplyKind::Abort}; ///< Which reply this is.
  uint16_t index{0};                      ///< Object index (initiate replies and abort).
  uint8_t sub{0};                         ///< Sub-index (initiate replies and abort).
  bool toggle{false};                     ///< Toggle bit (segment replies).
  bool last{false};                       ///< Last segment (segment upload replies).
  bool expedited{false};                  ///< Expedited transfer (initiate upload).
  bool size_indicated{false};             ///< `size` is valid (initiate upload).
  uint8_t data_len{0};                    ///< Valid bytes in `data`.
  uint32_t size{0};                       ///< Indicated size, or the abort code for an abort.
  std::array<uint8_t, 7> data{};          ///< Expedited data or the segment's bytes.
};

// --- Heartbeat, guarding, EMCY, PDO
// ---------------------------------------------------------------

/** @brief A decoded heartbeat or node-guarding reply. */
struct HeartbeatInfo {
  uint8_t node{0};                  ///< The node (identifier minus 0x700).
  NmtState state{NmtState::BootUp}; ///< The reported state.
  bool toggle{false};               ///< Bit 7: the node-guarding toggle (always 0 in a heartbeat).
};

/** @brief What the heartbeat consumer knows about one node. */
struct NodeStatus {
  NmtState state{NmtState::BootUp}; ///< The last reported state; meaningful when `seen`.
  bool seen{false};                 ///< At least one heartbeat or guarding reply arrived.
  bool bootup_seen{false};          ///< A boot-up message arrived since the node was registered.
  bool alive{false};                ///< The last heartbeat is younger than the registered timeout.
  uint32_t last_us{0};              ///< `NowUs()` at the last heartbeat.
  uint32_t count{0};                ///< Heartbeats and guarding replies received.
};

/** @brief One emergency message (EMCY). */
struct EmcyEvent {
  uint8_t node{0};                       ///< The node that sent it.
  uint16_t code{0};                      ///< The emergency error code; 0x0000 means error reset.
  uint8_t error_register{0};             ///< Object 0x1001 as the node reported it.
  std::array<uint8_t, 5> manufacturer{}; ///< Manufacturer-specific bytes.
  uint32_t timestamp_us{0};              ///< `NowUs()` when the frame was received.
};

/** @brief The latest frame received on a subscribed TPDO identifier. */
struct PdoSample {
  uint16_t cob_id{0};            ///< The identifier.
  uint8_t dlc{0};                ///< Valid bytes in `data`.
  std::array<uint8_t, 8> data{}; ///< The payload.
  uint32_t sequence{0};          ///< Frames received on this identifier; 0 means none yet.
  uint32_t timestamp_us{0};      ///< `NowUs()` at the last frame.
};

/** @brief What `Poll()` did with the frame it received. */
enum class FrameClass : uint8_t {
  None = 0,  ///< No frame arrived within the timeout.
  Heartbeat, ///< A heartbeat or boot-up message; the node table is updated.
  Emcy,      ///< An emergency message; queued for `PopEmcy()`.
  Pdo,       ///< A subscribed TPDO; its sample is updated.
  Sync,      ///< A SYNC frame from another producer.
  Ignored,   ///< A valid frame nobody here asked for (including a stale SDO reply).
  Invalid,   ///< A frame that fails validation; counted and dropped.
};

/** @brief Counters of the receive path, for diagnostics. */
struct Stats {
  uint32_t frames_received{0}; ///< Frames the port delivered.
  uint32_t frames_invalid{0};  ///< Frames dropped by validation.
  uint32_t sdo_stale{0};       ///< SDO replies that matched no outstanding request.
  uint32_t emcy_dropped{0};    ///< EMCY events dropped because the queue was full.
  uint32_t flood_aborts{0};    ///< Waits that gave up after `kMaxFramesPerWait` frames.
};

/** @brief Whether a PDO is a receive PDO (client to node) or a transmit PDO (node to client). */
enum class PdoKind : uint8_t {
  Receive = 0,  ///< RPDO: the node consumes it.
  Transmit = 1, ///< TPDO: the node produces it.
};

/** @brief One mapped object of a PDO. */
struct PdoMapEntry {
  uint16_t index{0}; ///< Object index.
  uint8_t sub{0};    ///< Sub-index.
  uint8_t bits{0};   ///< Length in bits, 1 to 64.
};

/** @brief The communication and mapping parameters of one PDO. */
struct PdoConfig {
  uint16_t cob_id{0};              ///< The identifier (`kPdoCobIdMin` to `kPdoCobIdMax`).
  uint8_t transmission_type{0xFF}; ///< 0 to 240 synchronous, 252 and 253 remote-request only, 254
                                   ///< and 255 event driven; 241 to 251 are reserved and refused.
  uint16_t inhibit_time_100us{0};  ///< Inhibit time (TPDO), 0 leaves the node's value.
  uint16_t event_timer_ms{0};      ///< Event timer (TPDO), 0 leaves the node's value.
  bool enabled{true};              ///< The PDO is valid (COB-ID bit 31 clear) when configured.
  uint8_t entry_count{0};          ///< Valid entries, 0 to `kMaxPdoEntries`.
  std::array<PdoMapEntry, kMaxPdoEntries> entries{}; ///< The mapping.
};

// --- CiA 402 -----------------------------------------------------------------------------------

/** @brief The drive state machine states, decoded from the statusword. */
enum class DriveState : uint8_t {
  NotReadyToSwitchOn = 0,
  SwitchOnDisabled,
  ReadyToSwitchOn,
  SwitchedOn,
  OperationEnabled,
  QuickStopActive,
  FaultReactionActive,
  Fault,
  Unknown, ///< The statusword matches no state.
};

/**
 * @brief The name of a drive state, for logs.
 * @param state The state.
 * @return A static string; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(DriveState state) noexcept {
  switch (state) {
  case DriveState::NotReadyToSwitchOn:
    return "NotReadyToSwitchOn";
  case DriveState::SwitchOnDisabled:
    return "SwitchOnDisabled";
  case DriveState::ReadyToSwitchOn:
    return "ReadyToSwitchOn";
  case DriveState::SwitchedOn:
    return "SwitchedOn";
  case DriveState::OperationEnabled:
    return "OperationEnabled";
  case DriveState::QuickStopActive:
    return "QuickStopActive";
  case DriveState::FaultReactionActive:
    return "FaultReactionActive";
  case DriveState::Fault:
    return "Fault";
  case DriveState::Unknown:
    return "Unknown";
  default:
    break;
  }
  return "Unknown";
}

/** @brief Modes of operation (object 0x6060), as the profile numbers them. */
enum class OperationMode : int8_t {
  NoMode = 0,
  ProfilePosition = 1,
  Velocity = 2,
  ProfileVelocity = 3,
  ProfileTorque = 4,
  Homing = 6,
  InterpolatedPosition = 7,
  CyclicSynchronousPosition = 8,
  CyclicSynchronousVelocity = 9,
  CyclicSynchronousTorque = 10,
};

/** @brief The object indices of a drive; the defaults are the profile's, a remapped drive overrides
 * them. */
struct DriveObjects {
  uint16_t controlword{0x6040};  ///< Controlword.
  uint16_t statusword{0x6041};   ///< Statusword.
  uint16_t mode{0x6060};         ///< Modes of operation.
  uint16_t mode_display{0x6061}; ///< Modes of operation display.
};

/// Controlword: shutdown (to Ready to switch on).
inline constexpr uint16_t kControlShutdown = 0x0006;
/// Controlword: switch on (to Switched on).
inline constexpr uint16_t kControlSwitchOn = 0x0007;
/// Controlword: disable voltage (to Switch on disabled).
inline constexpr uint16_t kControlDisableVoltage = 0x0000;
/// Controlword: quick stop.
inline constexpr uint16_t kControlQuickStop = 0x0002;
/// Controlword: enable operation (to Operation enabled).
inline constexpr uint16_t kControlEnableOperation = 0x000F;
/// Controlword: fault reset (bit 7, acts on the rising edge).
inline constexpr uint16_t kControlFaultReset = 0x0080;

// --- Configuration -----------------------------------------------------------------------------

/** @brief Everything the client uses; the defaults suit a node on a 500 kbit/s bus. */
struct Config {
  uint32_t sdo_timeout_us{200'000};        ///< Wait for each SDO reply, 1 ms to 60 s.
  uint32_t guard_timeout_us{100'000};      ///< Wait for a node-guarding reply, 1 ms to 10 s.
  uint32_t drive_timeout_us{2'000'000};    ///< Wait for a drive state change, 1 ms to 60 s.
  uint32_t drive_poll_interval_us{10'000}; ///< Pause between statusword reads, 100 us to 1 s.
  uint8_t safety_attempts{3};   ///< Sends of a stop or disable command before it gives up, 1 to 8.
  bool abort_on_timeout{true};  ///< Send an SDO abort (0x05040000) to the node after a timeout.
  bool strict_sdo_length{true}; ///< Require every SDO frame to carry 8 data bytes.
};

/** @brief Longest SDO timeout in microseconds. */
inline constexpr uint32_t kMaxSdoTimeoutUs = 60'000'000;

/**
 * @brief Check a configuration without touching the bus.
 * @param config The configuration.
 * @return `Error::Ok` or `Error::InvalidArgument`.
 */
constexpr Error Validate(const Config& config) noexcept {
  const bool sdo_ok =
      (config.sdo_timeout_us >= 1'000U) && (config.sdo_timeout_us <= kMaxSdoTimeoutUs);
  const bool guard_ok =
      (config.guard_timeout_us >= 1'000U) && (config.guard_timeout_us <= 10'000'000U);
  const bool drive_ok =
      (config.drive_timeout_us >= 1'000U) && (config.drive_timeout_us <= kMaxSdoTimeoutUs);
  const bool poll_ok =
      (config.drive_poll_interval_us >= 100U) && (config.drive_poll_interval_us <= 1'000'000U);
  const bool attempts_ok = (config.safety_attempts >= 1U) && (config.safety_attempts <= 8U);
  if (sdo_ok && guard_ok && drive_ok && poll_ok && attempts_ok) {
    return Error::Ok;
  }
  return Error::InvalidArgument;
}

/**
 * @brief Short timeouts for a fast, lightly loaded bus.
 * @return The preset.
 */
constexpr Config FastBusPreset() noexcept {
  Config c{};
  c.sdo_timeout_us = 50'000;
  c.guard_timeout_us = 20'000;
  c.drive_poll_interval_us = 2'000;
  return c;
}

/**
 * @brief Long timeouts for a slow or busy bus (for example 125 kbit/s with many nodes).
 * @return The preset.
 */
constexpr Config SlowBusPreset() noexcept {
  Config c{};
  c.sdo_timeout_us = 1'000'000;
  c.guard_timeout_us = 500'000;
  c.drive_timeout_us = 5'000'000;
  c.drive_poll_interval_us = 50'000;
  return c;
}

/** @return The driver version, e.g. `"0.1.0"`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return CANOPEN_CLIENT_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return CANOPEN_CLIENT_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return CANOPEN_CLIENT_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return CANOPEN_CLIENT_VERSION_PATCH;
}

/** @} */

} // namespace canopen_client

#endif // CANOPEN_CLIENT_TYPES_HPP

/**
 * @file canopen_client_protocol.hpp
 * @brief The CANopen frame codecs: pure functions that build and decode NMT, SYNC, SDO, heartbeat,
 * EMCY and PDO frames and the drive statusword. No I/O, no state.
 * @copyright Copyright (c) 2026 The driver-canopen-client authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Every builder validates its arguments and returns `Error::InvalidArgument` for a bad one, so no
 * frame with a wrong identifier or length is ever built. Every decoder validates the frame it
 * reads (identifier, length, command specifier) and returns `Error::InvalidFrame` for a frame
 * that does not fit; it never reads past `dlc`. The fuzz target drives them with arbitrary frames.
 */
#ifndef CANOPEN_CLIENT_PROTOCOL_HPP
#define CANOPEN_CLIENT_PROTOCOL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "canopen_client_detail.hpp"
#include "canopen_client_types.hpp"

namespace canopen_client {

/**
 * @defgroup canopen_client_protocol Frame codecs
 * @brief Pure builders and decoders for every frame the client sends or reads.
 * @{
 */

/**
 * @brief Whether a node identifier is in 1 to 127.
 * @param node The node identifier.
 * @return true when valid (0, the broadcast address, is not).
 */
constexpr bool IsNodeId(uint8_t node) noexcept {
  return (node >= kMinNodeId) && (node <= kMaxNodeId);
}

/**
 * @brief The identifier of the SDO request frames to `node`.
 * @param node The node, 1 to 127 (not checked here).
 * @return 0x600 + node.
 */
constexpr uint16_t SdoRequestId(uint8_t node) noexcept {
  return static_cast<uint16_t>(detail::Widen(kCobIdSdoRequestBase) + detail::Widen(node));
}

/**
 * @brief The identifier of the SDO reply frames from `node`.
 * @param node The node, 1 to 127 (not checked here).
 * @return 0x580 + node.
 */
constexpr uint16_t SdoResponseId(uint8_t node) noexcept {
  return static_cast<uint16_t>(detail::Widen(kCobIdSdoResponseBase) + detail::Widen(node));
}

/**
 * @brief The identifier of the heartbeat and node-guarding frames of `node`.
 * @param node The node, 1 to 127 (not checked here).
 * @return 0x700 + node.
 */
constexpr uint16_t HeartbeatId(uint8_t node) noexcept {
  return static_cast<uint16_t>(detail::Widen(kCobIdHeartbeatBase) + detail::Widen(node));
}

/**
 * @brief The identifier of the emergency frames of `node`.
 * @param node The node, 1 to 127 (not checked here).
 * @return 0x080 + node.
 */
constexpr uint16_t EmcyId(uint8_t node) noexcept {
  return static_cast<uint16_t>(detail::Widen(kCobIdEmcyBase) + detail::Widen(node));
}

/**
 * @brief Whether a command byte is one of the five NMT commands.
 * @param command The byte.
 * @return true when `command` is a defined `NmtCommand`.
 */
constexpr bool IsNmtCommand(uint8_t command) noexcept {
  return (command == static_cast<uint8_t>(NmtCommand::Start)) ||
         (command == static_cast<uint8_t>(NmtCommand::Stop)) ||
         (command == static_cast<uint8_t>(NmtCommand::PreOperational)) ||
         (command == static_cast<uint8_t>(NmtCommand::ResetNode)) ||
         (command == static_cast<uint8_t>(NmtCommand::ResetCommunication));
}

/**
 * @brief Decode the state byte of a heartbeat (bit 7 already cleared).
 * @param raw The state, 0, 4, 5 or 127.
 * @param[out] state The state; unchanged when `raw` is not a state.
 * @return true when `raw` is a defined state.
 */
constexpr bool DecodeNmtState(uint8_t raw, NmtState& state) noexcept {
  switch (raw) {
  case static_cast<uint8_t>(NmtState::BootUp):
    state = NmtState::BootUp;
    return true;
  case static_cast<uint8_t>(NmtState::Stopped):
    state = NmtState::Stopped;
    return true;
  case static_cast<uint8_t>(NmtState::Operational):
    state = NmtState::Operational;
    return true;
  case static_cast<uint8_t>(NmtState::PreOperational):
    state = NmtState::PreOperational;
    return true;
  default:
    break;
  }
  return false;
}

// --- Builders ----------------------------------------------------------------------------------

/**
 * @brief The NMT command frame: identifier 0x000, two data bytes.
 * @param command The command.
 * @param node The target, 1 to 127, or 0 for every node.
 * @return The frame, or `InvalidArgument` (node above 127 or a value that is not a command).
 */
constexpr Result<CanFrame> MakeNmtFrame(NmtCommand command, uint8_t node) noexcept {
  using detail::operator""_u8;
  const auto raw = static_cast<uint8_t>(command);
  if ((node > kMaxNodeId) || !IsNmtCommand(raw)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f{};
  f.id = kCobIdNmt;
  f.dlc = 2_u8;
  f.data[0] = raw;
  f.data[1] = node;
  return Result<CanFrame>::Success(f);
}

/**
 * @brief The SYNC frame: identifier 0x080, no data, or one counter byte.
 * @param counter 0 for a SYNC without data; 1 to 240 for the counter byte; else `InvalidArgument`.
 * @return The frame.
 */
constexpr Result<CanFrame> MakeSyncFrame(uint8_t counter) noexcept {
  using detail::operator""_u8;
  if (counter > 240_u8) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f{};
  f.id = kCobIdSync;
  if (counter != 0_u8) {
    f.dlc = 1_u8;
    f.data[0] = counter;
  }
  return Result<CanFrame>::Success(f);
}

/**
 * @brief The node-guarding request: a remote frame on 0x700 + node.
 * @param node The node, 1 to 127.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeGuardRequest(uint8_t node) noexcept {
  if (!IsNodeId(node)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f{};
  f.id = HeartbeatId(node);
  f.rtr = true;
  return Result<CanFrame>::Success(f);
}

/**
 * @brief A PDO frame: a valid identifier and 0 to 8 data bytes.
 * @param cob_id The identifier, `kPdoCobIdMin` to `kPdoCobIdMax`.
 * @param data The payload, at most 8 bytes.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakePdoFrame(uint16_t cob_id, std::span<const uint8_t> data) noexcept {
  if ((cob_id < kPdoCobIdMin) || (cob_id > kPdoCobIdMax) || (data.size() > kMaxDlc)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f{};
  f.id = cob_id;
  f.dlc = static_cast<uint8_t>(data.size());
  for (std::size_t i = 0; i < data.size(); ++i) {
    f.data[i] = data[i];
  }
  return Result<CanFrame>::Success(f);
}

namespace detail {

/// An SDO request frame with the command byte and the multiplexer filled in.
constexpr CanFrame SdoRequestBase(uint8_t node, uint8_t command, uint16_t index,
                                  uint8_t sub) noexcept {
  using detail::operator""_u8;
  CanFrame f{};
  f.id = SdoRequestId(node);
  f.dlc = 8_u8;
  f.data[0] = command;
  f.data[1] = static_cast<uint8_t>(Widen(index) & 0xFFU);
  f.data[2] = static_cast<uint8_t>(Widen(index) >> 8U);
  f.data[3] = sub;
  return f;
}

} // namespace detail

/**
 * @brief The initiate-upload request (read an object): command byte 0x40.
 * @param node The server, 1 to 127.
 * @param index The object index.
 * @param sub The sub-index.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeSdoUploadRequest(uint8_t node, uint16_t index,
                                                uint8_t sub) noexcept {
  using detail::operator""_u8;
  if (!IsNodeId(node)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  return Result<CanFrame>::Success(detail::SdoRequestBase(node, 0x40_u8, index, sub));
}

/**
 * @brief The expedited initiate-download request (write 1 to 4 bytes): command byte
 * 0x23 | (4 - size) << 2.
 * @param node The server, 1 to 127.
 * @param index The object index.
 * @param sub The sub-index.
 * @param data 1 to 4 bytes.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeSdoExpeditedDownload(uint8_t node, uint16_t index, uint8_t sub,
                                                    std::span<const uint8_t> data) noexcept {
  if (!IsNodeId(node) || data.empty() || (data.size() > 4U)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  const auto unused = static_cast<uint32_t>(4U - data.size());
  const auto command = static_cast<uint8_t>(0x23U | (unused << 2U));
  CanFrame f = detail::SdoRequestBase(node, command, index, sub);
  for (std::size_t i = 0; i < data.size(); ++i) {
    f.data[4U + i] = data[i];
  }
  return Result<CanFrame>::Success(f);
}

/**
 * @brief The segmented initiate-download request announcing `size` bytes: command byte 0x21.
 * @param node The server, 1 to 127.
 * @param index The object index.
 * @param sub The sub-index.
 * @param size The number of bytes that follow, at least 1.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeSdoSegmentedDownload(uint8_t node, uint16_t index, uint8_t sub,
                                                    uint32_t size) noexcept {
  using detail::operator""_u8;
  if (!IsNodeId(node) || (size == 0U)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f = detail::SdoRequestBase(node, 0x21_u8, index, sub);
  detail::StoreLe32At4(f.data, size);
  return Result<CanFrame>::Success(f);
}

/**
 * @brief One download segment: command byte toggle << 4 | (7 - length) << 1 | last.
 * @param node The server, 1 to 127.
 * @param toggle The toggle bit, starting at 0 and alternating.
 * @param data 1 to 7 bytes.
 * @param last true for the final segment.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeSdoDownloadSegment(uint8_t node, bool toggle,
                                                  std::span<const uint8_t> data,
                                                  bool last) noexcept {
  using detail::operator""_u8;
  if (!IsNodeId(node) || data.empty() || (data.size() > 7U)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  const uint32_t unused = 7U - static_cast<uint32_t>(data.size());
  const uint32_t command = (toggle ? 0x10U : 0U) | (unused << 1U) | (last ? 1U : 0U);
  CanFrame f{};
  f.id = SdoRequestId(node);
  f.dlc = 8_u8;
  f.data[0] = static_cast<uint8_t>(command);
  for (std::size_t i = 0; i < data.size(); ++i) {
    f.data[1U + i] = data[i];
  }
  return Result<CanFrame>::Success(f);
}

/**
 * @brief The request for the next upload segment: command byte 0x60 | toggle << 4.
 * @param node The server, 1 to 127.
 * @param toggle The toggle bit, starting at 0 and alternating.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeSdoUploadSegmentRequest(uint8_t node, bool toggle) noexcept {
  using detail::operator""_u8;
  if (!IsNodeId(node)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f{};
  f.id = SdoRequestId(node);
  f.dlc = 8_u8;
  f.data[0] = toggle ? 0x70_u8 : 0x60_u8;
  return Result<CanFrame>::Success(f);
}

/**
 * @brief The abort frame the client sends to end a transfer: command byte 0x80.
 * @param node The server, 1 to 127.
 * @param index The object index of the transfer.
 * @param sub The sub-index of the transfer.
 * @param code The abort code.
 * @return The frame, or `InvalidArgument`.
 */
constexpr Result<CanFrame> MakeSdoAbort(uint8_t node, uint16_t index, uint8_t sub,
                                        uint32_t code) noexcept {
  using detail::operator""_u8;
  if (!IsNodeId(node)) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  CanFrame f = detail::SdoRequestBase(node, 0x80_u8, index, sub);
  detail::StoreLe32At4(f.data, code);
  return Result<CanFrame>::Success(f);
}

// --- Decoders ----------------------------------------------------------------------------------

namespace detail {

/// The initiate upload reply: expedited data or the indicated size.
constexpr Result<SdoReply> ParseInitiateUpload(const CanFrame& f, SdoReply r,
                                               bool strict) noexcept {
  using detail::operator""_u8;
  const uint32_t b0 = Widen(f.data[0]);
  const bool expedited = TestBit(b0, 1U);
  const bool sized = TestBit(b0, 0U);
  r.expedited = expedited;
  r.size_indicated = sized;
  if (expedited) {
    const uint32_t len = sized ? (4U - ((b0 >> 2U) & 3U)) : 4U;
    if ((Widen(f.dlc) < (4U + len)) || (strict && (f.dlc != 8_u8))) {
      return Result<SdoReply>::Failure(Error::InvalidFrame);
    }
    r.data_len = static_cast<uint8_t>(len);
    for (uint32_t i = 0U; i < len; ++i) {
      r.data[i] = f.data[4U + i];
    }
    return Result<SdoReply>::Success(r);
  }
  const uint32_t need = sized ? 8U : 4U;
  if ((Widen(f.dlc) < need) || (strict && (f.dlc != 8_u8))) {
    return Result<SdoReply>::Failure(Error::InvalidFrame);
  }
  r.size = sized ? LoadLe32At4(f.data) : 0U;
  return Result<SdoReply>::Success(r);
}

/// An upload segment: toggle, last flag and 0 to 7 data bytes.
constexpr Result<SdoReply> ParseUploadSegment(const CanFrame& f, SdoReply r, bool strict) noexcept {
  using detail::operator""_u8;
  const uint32_t b0 = Widen(f.data[0]);
  const uint32_t len = 7U - ((b0 >> 1U) & 7U);
  if ((Widen(f.dlc) < (1U + len)) || (strict && (f.dlc != 8_u8))) {
    return Result<SdoReply>::Failure(Error::InvalidFrame);
  }
  r.toggle = TestBit(b0, 4U);
  r.last = TestBit(b0, 0U);
  r.data_len = static_cast<uint8_t>(len);
  for (uint32_t i = 0U; i < len; ++i) {
    r.data[i] = f.data[1U + i];
  }
  return Result<SdoReply>::Success(r);
}

/// A download segment response: the toggle bit only.
constexpr Result<SdoReply> ParseDownloadSegment(const CanFrame& f, SdoReply r,
                                                bool strict) noexcept {
  using detail::operator""_u8;
  r.kind = SdoReplyKind::SegmentDownload;
  r.toggle = TestBit(Widen(f.data[0]), 4U);
  return (strict && (f.dlc != 8_u8)) ? Result<SdoReply>::Failure(Error::InvalidFrame)
                                     : Result<SdoReply>::Success(r);
}

/// An abort frame (command specifier 4): the 32-bit code; anything else here is not a reply.
constexpr Result<SdoReply> ParseAbort(const CanFrame& f, SdoReply r, uint32_t scs) noexcept {
  using detail::operator""_u8;
  if ((scs == 4U) && (f.dlc == 8_u8)) {
    r.kind = SdoReplyKind::Abort;
    r.size = LoadLe32At4(f.data);
    return Result<SdoReply>::Success(r);
  }
  return Result<SdoReply>::Failure(Error::InvalidFrame);
}

} // namespace detail

/**
 * @brief Decode a frame from an SDO server (identifier 0x580 + node).
 *
 * The identifier is the caller's to check; this reads the payload. Block-transfer command
 * specifiers (5 and 6) and the reserved 7 are not replies to a request this client sends and are
 * `InvalidFrame`.
 *
 * @param frame The frame.
 * @param strict true to require 8 data bytes in every SDO frame; false to accept a frame as short
 * as its content needs (4 bytes for an initiate reply, 1 for a segment reply).
 * @return The reply, or `InvalidFrame` (remote frame, too short, or an unusable command specifier).
 */
constexpr Result<SdoReply> ParseSdoReply(const CanFrame& frame, bool strict) noexcept {
  using detail::operator""_u8;
  if (frame.rtr || (frame.dlc < 1_u8) || (frame.dlc > kMaxDlc)) {
    return Result<SdoReply>::Failure(Error::InvalidFrame);
  }
  SdoReply r{};
  const uint32_t b0 = detail::Widen(frame.data[0]);
  const uint32_t scs = b0 >> 5U;
  const bool header_ok = (frame.dlc >= 4_u8);
  if (scs == 1U) { // download segment response
    return detail::ParseDownloadSegment(frame, r, strict);
  }
  if (scs == 0U) { // upload segment
    r.kind = SdoReplyKind::SegmentUpload;
    return detail::ParseUploadSegment(frame, r, strict);
  }
  if (!header_ok || ((scs != 4U) && strict && (frame.dlc != 8_u8))) {
    return Result<SdoReply>::Failure(Error::InvalidFrame);
  }
  r.index = detail::Le16(frame.data[1], frame.data[2]);
  r.sub = frame.data[3];
  if (scs == 3U) { // initiate download response
    r.kind = SdoReplyKind::InitiateDownload;
    return Result<SdoReply>::Success(r);
  }
  if (scs == 2U) { // initiate upload response
    r.kind = SdoReplyKind::InitiateUpload;
    return detail::ParseInitiateUpload(frame, r, strict);
  }
  return detail::ParseAbort(frame, r, scs);
}

/**
 * @brief Decode a heartbeat, boot-up or node-guarding reply: identifier 0x701 to 0x77F, one byte.
 * @param frame The frame.
 * @return The node, state and toggle bit, or `InvalidFrame` (remote frame, identifier outside the
 * range, length other than 1, or a state that is not 0, 4, 5 or 127).
 */
constexpr Result<HeartbeatInfo> ParseHeartbeat(const CanFrame& frame) noexcept {
  using detail::operator""_u8;
  const uint32_t id = detail::Widen(frame.id);
  const uint32_t base = detail::Widen(kCobIdHeartbeatBase);
  if (frame.rtr || (frame.dlc != 1_u8) || (id <= base) ||
      (id > (base + detail::Widen(kMaxNodeId)))) {
    return Result<HeartbeatInfo>::Failure(Error::InvalidFrame);
  }
  HeartbeatInfo h{};
  h.node = static_cast<uint8_t>(id - base);
  h.toggle = detail::TestBit(detail::Widen(frame.data[0]), 7U);
  const auto raw = static_cast<uint8_t>(detail::Widen(frame.data[0]) & 0x7FU);
  if (!DecodeNmtState(raw, h.state)) {
    return Result<HeartbeatInfo>::Failure(Error::InvalidFrame);
  }
  return Result<HeartbeatInfo>::Success(h);
}

/**
 * @brief Decode an emergency message: identifier 0x081 to 0x0FF, eight bytes (error code, error
 * register, five manufacturer bytes). The timestamp is left 0 for the caller.
 * @param frame The frame.
 * @return The event, or `InvalidFrame`.
 */
constexpr Result<EmcyEvent> ParseEmcy(const CanFrame& frame) noexcept {
  using detail::operator""_u8;
  const uint32_t id = detail::Widen(frame.id);
  const uint32_t base = detail::Widen(kCobIdEmcyBase);
  if (frame.rtr || (frame.dlc != 8_u8) || (id <= base) ||
      (id > (base + detail::Widen(kMaxNodeId)))) {
    return Result<EmcyEvent>::Failure(Error::InvalidFrame);
  }
  EmcyEvent e{};
  e.node = static_cast<uint8_t>(id - base);
  e.code = detail::Le16(frame.data[0], frame.data[1]);
  e.error_register = frame.data[2];
  for (std::size_t i = 0; i < e.manufacturer.size(); ++i) {
    e.manufacturer[i] = frame.data[3U + i];
  }
  return Result<EmcyEvent>::Success(e);
}

// --- PDO ----------------------------------------------------------------------------------------

/**
 * @brief The index of the communication parameter record of a PDO.
 * @param kind Receive (0x1400 + n - 1) or transmit (0x1800 + n - 1).
 * @param number The PDO number, 1 to 512.
 * @return The index, or `InvalidArgument`.
 */
constexpr Result<uint16_t> PdoCommunicationIndex(PdoKind kind, uint16_t number) noexcept {
  using detail::operator""_u16;
  if ((number < 1_u16) || (number > 512_u16)) {
    return Result<uint16_t>::Failure(Error::InvalidArgument);
  }
  const uint32_t base = (kind == PdoKind::Receive) ? 0x1400U : 0x1800U;
  return Result<uint16_t>::Success(static_cast<uint16_t>(base + detail::Widen(number) - 1U));
}

/**
 * @brief The index of the mapping parameter record of a PDO.
 * @param kind Receive (0x1600 + n - 1) or transmit (0x1A00 + n - 1).
 * @param number The PDO number, 1 to 512.
 * @return The index, or `InvalidArgument`.
 */
constexpr Result<uint16_t> PdoMappingIndex(PdoKind kind, uint16_t number) noexcept {
  const auto comm = PdoCommunicationIndex(kind, number);
  if (!comm) {
    return comm;
  }
  return Result<uint16_t>::Success(static_cast<uint16_t>(detail::Widen(comm.value) + 0x200U));
}

/**
 * @brief The 32-bit mapping record: index << 16 | sub-index << 8 | length in bits.
 * @param entry The mapped object.
 * @return The record.
 */
constexpr uint32_t EncodePdoMapEntry(const PdoMapEntry& entry) noexcept {
  return (detail::Widen(entry.index) << 16U) | (detail::Widen(entry.sub) << 8U) |
         detail::Widen(entry.bits);
}

/**
 * @brief Split a 32-bit mapping record into its object and length.
 * @param record The record.
 * @return The entry.
 */
constexpr PdoMapEntry DecodePdoMapEntry(uint32_t record) noexcept {
  PdoMapEntry e{};
  e.index = static_cast<uint16_t>(record >> 16U);
  e.sub = static_cast<uint8_t>((record >> 8U) & 0xFFU);
  e.bits = static_cast<uint8_t>(record & 0xFFU);
  return e;
}

/**
 * @brief Check a PDO configuration: identifier range, a transmission type that is not reserved (241
 * to 251), 0 to `kMaxPdoEntries` entries, each of 1 to 64 bits at a non-zero index, and at most 64
 * bits in all.
 * @param config The configuration.
 * @return `Ok` or `InvalidArgument`.
 */
constexpr Error ValidatePdoConfig(const PdoConfig& config) noexcept {
  using detail::operator""_u8;
  using detail::operator""_u16;
  if ((config.cob_id < kPdoCobIdMin) || (config.cob_id > kPdoCobIdMax) ||
      (config.entry_count > kMaxPdoEntries)) {
    return Error::InvalidArgument;
  }
  if ((config.transmission_type >= 241_u8) && (config.transmission_type <= 251_u8)) {
    return Error::InvalidArgument; // reserved
  }
  uint32_t total = 0U;
  for (std::size_t i = 0; i < config.entry_count; ++i) {
    const PdoMapEntry& e = config.entries[i];
    if ((e.index == 0_u16) || (e.bits < 1_u8) || (e.bits > 64_u8)) {
      return Error::InvalidArgument;
    }
    total += detail::Widen(e.bits);
  }
  return (total <= 64U) ? Error::Ok : Error::InvalidArgument;
}

/**
 * @brief The bits the entries of a PDO occupy in all.
 * @param config The configuration (not validated).
 * @return The sum of the entry lengths of the first `entry_count` entries.
 */
constexpr uint32_t PdoBitLength(const PdoConfig& config) noexcept {
  uint32_t total = 0U;
  for (std::size_t i = 0; (i < config.entry_count) && (i < config.entries.size()); ++i) {
    total += detail::Widen(config.entries[i].bits);
  }
  return total;
}

/**
 * @brief Read `bits` bits starting at bit `offset` of a PDO payload (Intel bit order: bit 0 is the
 * least significant bit of byte 0).
 * @param data The payload.
 * @param offset The first bit.
 * @param bits The width, 1 to 64.
 * @return The value, or `InvalidArgument` when the field is empty, wider than 64 bits or ends past
 * the payload.
 */
constexpr Result<uint64_t> ExtractBits(std::span<const uint8_t> data, uint32_t offset,
                                       uint32_t bits) noexcept {
  const uint64_t end = static_cast<uint64_t>(offset) + bits;
  if ((bits < 1U) || (bits > 64U) || (end > (static_cast<uint64_t>(data.size()) * 8U))) {
    return Result<uint64_t>::Failure(Error::InvalidArgument);
  }
  uint64_t value = 0U;
  for (uint32_t i = 0U; i < bits; ++i) {
    const uint32_t bit = offset + i;
    const uint32_t b = (detail::Widen(data[bit / 8U]) >> (bit % 8U)) & 1U;
    value |= static_cast<uint64_t>(b) << i;
  }
  return Result<uint64_t>::Success(value);
}

/**
 * @brief Write the low `bits` bits of `value` at bit `offset` of a PDO payload; the other bits are
 * unchanged.
 * @param data The payload.
 * @param offset The first bit.
 * @param bits The width, 1 to 64.
 * @param value The value; higher bits are ignored.
 * @return `Ok`, or `InvalidArgument` when the field does not fit in the payload.
 */
constexpr Result<void> InsertBits(std::span<uint8_t> data, uint32_t offset, uint32_t bits,
                                  uint64_t value) noexcept {
  const uint64_t end = static_cast<uint64_t>(offset) + bits;
  if ((bits < 1U) || (bits > 64U) || (end > (static_cast<uint64_t>(data.size()) * 8U))) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  for (uint32_t i = 0U; i < bits; ++i) {
    const uint32_t bit = offset + i;
    const uint32_t mask = 1U << (bit % 8U);
    const uint32_t old = detail::Widen(data[bit / 8U]);
    const bool set = ((value >> i) & 1U) != 0U;
    data[bit / 8U] = static_cast<uint8_t>(set ? (old | mask) : (old & ~mask));
  }
  return Result<void>::Success();
}

// --- CiA 402 -----------------------------------------------------------------------------------

/**
 * @brief Decode the drive state from a statusword (object 0x6041): the masks 0x4F and 0x6F of the
 * state machine, matched exactly.
 * @param statusword The statusword.
 * @return The state, or `DriveState::Unknown` for a bit pattern that is none of them.
 */
constexpr DriveState DecodeStatusword(uint16_t statusword) noexcept {
  const uint32_t sw = detail::Widen(statusword);
  const uint32_t low = sw & 0x4FU;
  const uint32_t quick = sw & 0x6FU;
  if (low == 0x00U) {
    return DriveState::NotReadyToSwitchOn;
  }
  if (low == 0x40U) {
    return DriveState::SwitchOnDisabled;
  }
  if (low == 0x0FU) {
    return DriveState::FaultReactionActive;
  }
  if (low == 0x08U) {
    return DriveState::Fault;
  }
  if (quick == 0x21U) {
    return DriveState::ReadyToSwitchOn;
  }
  if (quick == 0x23U) {
    return DriveState::SwitchedOn;
  }
  if (quick == 0x27U) {
    return DriveState::OperationEnabled;
  }
  if (quick == 0x07U) {
    return DriveState::QuickStopActive;
  }
  return DriveState::Unknown;
}

/**
 * @brief The controlword that moves a drive one step toward Operation enabled.
 * @param state The drive's present state.
 * @param[out] word The controlword; unchanged when no write helps.
 * @return true when `word` is to be written: Switch on disabled (shutdown), Ready to switch on
 * (switch on), Switched on (enable operation), Quick stop active (disable voltage). false for the
 * states that only wait (Not ready to switch on, Fault reaction active) or end the walk (Operation
 * enabled, Fault, Unknown).
 */
constexpr bool EnableStepControlword(DriveState state, uint16_t& word) noexcept {
  switch (state) {
  case DriveState::SwitchOnDisabled:
    word = kControlShutdown;
    return true;
  case DriveState::ReadyToSwitchOn:
    word = kControlSwitchOn;
    return true;
  case DriveState::SwitchedOn:
    word = kControlEnableOperation;
    return true;
  case DriveState::QuickStopActive:
    word = kControlDisableVoltage;
    return true;
  case DriveState::NotReadyToSwitchOn:
  case DriveState::OperationEnabled:
  case DriveState::FaultReactionActive:
  case DriveState::Fault:
  case DriveState::Unknown:
    break;
  default:
    break;
  }
  return false;
}

/** @} */

} // namespace canopen_client

#endif // CANOPEN_CLIENT_PROTOCOL_HPP

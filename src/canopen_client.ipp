/**
 * @file canopen_client.ipp
 * @brief CANopen client driver implementation, included by `canopen_client.hpp`.
 * @copyright Copyright (c) 2026 The driver-canopen-client authors. SPDX-License-Identifier: MIT
 */
#ifndef CANOPEN_CLIENT_IPP
#define CANOPEN_CLIENT_IPP

namespace canopen_client {

// --- Lifecycle ---------------------------------------------------------------------------------

template <CanPort Port>
Result<void> Client<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  if (Validate(config_) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!port_.EnsureInitialized()) {
    detail::Log(port_, LogLevel::Error, "canopen_client: the port could not start the controller");
    return Result<void>::Failure(Error::Bus);
  }
  resetState();
  initialized_ = true;
  return Result<void>::Success();
}

template <CanPort Port>
Result<void> Client<Port>::Deinitialize() noexcept {
  initialized_ = false;
  resetState();
  return Result<void>::Success();
}

template <CanPort Port>
Result<void> Client<Port>::SetConfig(const Config& config) noexcept {
  if (Validate(config) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  config_ = config;
  return Result<void>::Success();
}

template <CanPort Port>
void Client<Port>::resetState() noexcept {
  pending_ = detail::Pending{};
  last_abort_ = SdoAbortInfo{};
  stats_ = Stats{};
  monitors_.fill(detail::MonitorSlot{});
  pdos_.fill(detail::PdoSlot{});
  emcy_head_ = 0U;
  emcy_count_ = 0U;
  sync_period_us_ = 0U;
  sync_overflow_ = 0U;
  sync_counter_ = 0U;
  sync_started_ = false;
  sync_last_us_ = 0U;
}

template <CanPort Port>
Result<void> Client<Port>::sendFrame(const CanFrame& frame) noexcept {
  if (!IsValid(frame)) {
    return Result<void>::Failure(Error::InvalidFrame);
  }
  tx_ = frame; // the port sees a member that outlives this call, never a caller's local
  if (!port_.Send(tx_)) {
    detail::Log(port_, LogLevel::Error, "canopen_client: the port refused a frame");
    return Result<void>::Failure(Error::Bus);
  }
  return Result<void>::Success();
}

template <CanPort Port>
detail::MonitorSlot* Client<Port>::findSlot(uint8_t node) noexcept {
  for (detail::MonitorSlot& slot : monitors_) {
    // cppcheck-suppress useStlAlgorithm ; a pointer-returning search, clearer as a bounded loop
    if (slot.used && (slot.node == node)) {
      return &slot;
    }
  }
  return nullptr;
}

// --- NMT, SYNC, RPDO and unconfirmed SDO (every port)
// ---------------------------------------------

template <CanPort Port>
void Client<Port>::noteNmt(NmtCommand command, uint8_t node) noexcept {
  const bool reset =
      (command == NmtCommand::ResetNode) || (command == NmtCommand::ResetCommunication);
  for (detail::MonitorSlot& slot : monitors_) {
    if (!slot.used || ((node != kBroadcastNode) && (slot.node != node))) {
      continue;
    }
    if (reset) {
      slot.toggle = false; // the first guarding reply after a reset has the toggle bit clear
      slot.status.seen = false;
      slot.status.bootup_seen = false;
    } else if (slot.status.seen) {
      // The state the node was last heard in is out of date; expect the commanded one until its
      // next heartbeat says otherwise. This lifts the Stopped block as soon as a node is started
      // again.
      if (command == NmtCommand::Start) {
        slot.status.state = NmtState::Operational;
      } else if (command == NmtCommand::Stop) {
        slot.status.state = NmtState::Stopped;
      } else {
        slot.status.state = NmtState::PreOperational;
      }
    } else {
      // never heard from: nothing to correct
    }
  }
}

template <CanPort Port>
Result<void> Client<Port>::SendNmt(NmtCommand command, uint8_t node) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto frame = MakeNmtFrame(command, node);
  if (!frame) {
    return Result<void>::Failure(frame.error);
  }
  const auto sent = sendFrame(frame.value);
  if (sent) {
    noteNmt(command, node);
  }
  return sent;
}

template <CanPort Port>
Result<void> Client<Port>::SendSync(uint8_t counter) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto frame = MakeSyncFrame(counter);
  if (!frame) {
    return Result<void>::Failure(frame.error);
  }
  return sendFrame(frame.value);
}

template <CanPort Port>
Result<void> Client<Port>::SendRpdo(uint16_t cob_id, std::span<const uint8_t> data) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto frame = MakePdoFrame(cob_id, data);
  if (!frame) {
    return Result<void>::Failure(frame.error);
  }
  return sendFrame(frame.value);
}

template <CanPort Port>
Result<void> Client<Port>::SdoDownloadUnconfirmed(uint8_t node, uint16_t index, uint8_t sub,
                                                  std::span<const uint8_t> data) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto frame = MakeSdoExpeditedDownload(node, index, sub, data);
  if (!frame) {
    return Result<void>::Failure(frame.error);
  }
  return sendFrame(frame.value);
}

// --- Routing of received frames
// ---------------------------------------------------------------------

template <CanPort Port>
void Client<Port>::clearPending() noexcept {
  pending_.kind = detail::PendingKind::None;
}

template <CanPort Port>
void Client<Port>::finish(Error error, uint32_t abort_code) noexcept {
  pending_.done = true;
  pending_.error = error;
  pending_.abort_code = abort_code;
}

template <CanPort Port>
FrameClass Client<Port>::routeHeartbeat(const CanFrame& frame) noexcept
  requires CanRxPort<Port>
{
  const auto hb = ParseHeartbeat(frame);
  if (!hb) {
    detail::Bump(stats_.frames_invalid);
    return FrameClass::Invalid;
  }
  detail::MonitorSlot* slot = findSlot(hb.value.node);
  if (hb.value.toggle || (slot == nullptr)) {
    return FrameClass::Ignored; // an unsolicited guarding-style reply, or a node nobody watches
  }
  slot->status.state = hb.value.state;
  slot->status.seen = true;
  slot->status.bootup_seen = slot->status.bootup_seen || (hb.value.state == NmtState::BootUp);
  slot->status.last_us = port_.NowUs();
  detail::Bump(slot->status.count);
  return FrameClass::Heartbeat;
}

template <CanPort Port>
FrameClass Client<Port>::routeEmcy(const CanFrame& frame) noexcept
  requires CanRxPort<Port>
{
  auto event = ParseEmcy(frame);
  if (!event) {
    detail::Bump(stats_.frames_invalid);
    return FrameClass::Invalid;
  }
  event.value.timestamp_us = port_.NowUs();
  if (emcy_count_ >= emcy_.size()) {
    detail::Bump(stats_.emcy_dropped);
    return FrameClass::Emcy;
  }
  emcy_[(emcy_head_ + emcy_count_) % emcy_.size()] = event.value;
  ++emcy_count_;
  return FrameClass::Emcy;
}

template <CanPort Port>
FrameClass Client<Port>::routePdo(const CanFrame& frame) noexcept
  requires CanRxPort<Port>
{
  for (detail::PdoSlot& slot : pdos_) {
    if (!slot.used || (slot.sample.cob_id != frame.id)) {
      continue;
    }
    if (frame.dlc != slot.dlc) {
      detail::Bump(stats_.frames_invalid);
      return FrameClass::Invalid;
    }
    slot.sample.dlc = frame.dlc;
    slot.sample.data = frame.data;
    slot.sample.timestamp_us = port_.NowUs();
    detail::Bump(slot.sample.sequence);
    return FrameClass::Pdo;
  }
  return FrameClass::Ignored;
}

template <CanPort Port>
FrameClass Client<Port>::route(const CanFrame& frame) noexcept
  requires CanRxPort<Port>
{
  detail::Bump(stats_.frames_received);
  if (!IsValid(frame)) {
    detail::Bump(stats_.frames_invalid);
    return FrameClass::Invalid;
  }
  if (frame.rtr) {
    return FrameClass::Ignored;
  }
  const uint32_t id = detail::Widen(frame.id);
  if ((pending_.kind == detail::PendingKind::Sdo) && (frame.id == SdoResponseId(pending_.node))) {
    onSdoReply(frame);
    return FrameClass::Ignored;
  }
  if ((pending_.kind == detail::PendingKind::RawSdo) &&
      (frame.id == SdoResponseId(pending_.node))) {
    last_raw_ = frame; // taken as it is: the caller wants to see what the node sent
    finish(Error::Ok, 0U);
    return FrameClass::Ignored;
  }
  if ((pending_.kind == detail::PendingKind::Guard) && (frame.id == HeartbeatId(pending_.node))) {
    onGuardReply(frame);
    return FrameClass::Ignored;
  }
  if (id == detail::Widen(kCobIdSync)) {
    return FrameClass::Sync;
  }
  if ((id > detail::Widen(kCobIdEmcyBase)) && (id <= (detail::Widen(kCobIdEmcyBase) + 127U))) {
    return routeEmcy(frame);
  }
  if ((id > detail::Widen(kCobIdSdoResponseBase)) &&
      (id <= (detail::Widen(kCobIdSdoResponseBase) + 127U))) {
    detail::Bump(stats_.sdo_stale); // a reply nobody is waiting for
    return FrameClass::Ignored;
  }
  if ((id > detail::Widen(kCobIdHeartbeatBase)) &&
      (id <= (detail::Widen(kCobIdHeartbeatBase) + 127U))) {
    return routeHeartbeat(frame);
  }
  return routePdo(frame);
}

template <CanPort Port>
void Client<Port>::onSdoReply(const CanFrame& frame) noexcept
  requires CanRxPort<Port>
{
  const auto parsed = ParseSdoReply(frame, config_.strict_sdo_length);
  if (!parsed) {
    detail::Bump(stats_.frames_invalid);
    finish(Error::Protocol, static_cast<uint32_t>(SdoAbortCode::InvalidCommandSpecifier));
    return;
  }
  const SdoReply& r = parsed.value;
  const bool multiplexed = (r.kind == SdoReplyKind::Abort) ||
                           (r.kind == SdoReplyKind::InitiateDownload) ||
                           (r.kind == SdoReplyKind::InitiateUpload);
  if (multiplexed && ((r.index != pending_.index) || (r.sub != pending_.sub))) {
    detail::Bump(stats_.sdo_stale); // another object's reply: not the answer to this request
    return;
  }
  if (r.kind == SdoReplyKind::Abort) {
    last_abort_ = SdoAbortInfo{.node = pending_.node,
                               .index = pending_.index,
                               .sub = pending_.sub,
                               .code = r.size,
                               .remote = true};
    finish(Error::SdoAbort, 0U);
    return;
  }
  if (r.kind != pending_.expect) {
    finish(Error::Protocol, static_cast<uint32_t>(SdoAbortCode::InvalidCommandSpecifier));
    return;
  }
  const bool segment =
      (r.kind == SdoReplyKind::SegmentDownload) || (r.kind == SdoReplyKind::SegmentUpload);
  if (segment && (r.toggle != pending_.toggle)) {
    finish(Error::Protocol, static_cast<uint32_t>(SdoAbortCode::ToggleNotAlternated));
    return;
  }
  pending_.reply = r;
  finish(Error::Ok, 0U);
}

template <CanPort Port>
void Client<Port>::onGuardReply(const CanFrame& frame) noexcept
  requires CanRxPort<Port>
{
  const auto hb = ParseHeartbeat(frame);
  detail::MonitorSlot* slot = findSlot(pending_.node);
  if (!hb) {
    detail::Bump(stats_.frames_invalid); // not a reply: keep waiting
    return;
  }
  if (slot == nullptr) {
    finish(Error::InvalidArgument, 0U);
    return;
  }
  if (hb.value.state == NmtState::BootUp) {
    // A node reset while the request was out: its boot-up message is not the guard reply (its
    // first reply follows, toggle clear). Record the boot-up and keep waiting.
    static_cast<void>(routeHeartbeat(frame));
    return;
  }
  if (hb.value.toggle != slot->toggle) {
    slot->toggle = !hb.value.toggle; // resynchronise on the reply that came
    finish(Error::Protocol, 0U);
    return;
  }
  slot->toggle = !slot->toggle;
  slot->status.state = hb.value.state;
  slot->status.seen = true;
  slot->status.last_us = port_.NowUs();
  detail::Bump(slot->status.count);
  pending_.state = hb.value.state;
  finish(Error::Ok, 0U);
}

template <CanPort Port>
Result<void> Client<Port>::awaitPending(uint32_t timeout_us) noexcept
  requires CanRxPort<Port>
{
  const uint32_t start = port_.NowUs();
  uint32_t frames = 0U; // only frames count against the flood budget, not early timeouts
  for (uint32_t reads = 0U; reads < kMaxReceivesPerWait; ++reads) {
    const uint32_t elapsed = port_.NowUs() - start; // unsigned: correct across a counter wrap
    if (elapsed >= timeout_us) {
      return Result<void>::Failure(Error::Timeout);
    }
    const RxStatus status = port_.Receive(rx_, timeout_us - elapsed);
    if (status == RxStatus::Error) {
      return Result<void>::Failure(Error::Bus);
    }
    if (status == RxStatus::Frame) {
      static_cast<void>(route(rx_));
      if (pending_.done) {
        return (pending_.error == Error::Ok) ? Result<void>::Success()
                                             : Result<void>::Failure(pending_.error);
      }
      ++frames;
      if (frames >= kMaxFramesPerWait) {
        detail::Bump(stats_.flood_aborts);
        return Result<void>::Failure(Error::Timeout);
      }
    }
  }
  return Result<void>::Failure(Error::Timeout);
}

template <CanPort Port>
Result<void> Client<Port>::drainRx() noexcept
  requires CanRxPort<Port>
{
  clearPending();
  for (uint32_t i = 0U; i < kMaxDrainFrames; ++i) {
    const RxStatus status = port_.Receive(rx_, 0U);
    if (status == RxStatus::Error) {
      return Result<void>::Failure(Error::Bus);
    }
    if (status != RxStatus::Frame) {
      break;
    }
    static_cast<void>(route(rx_));
  }
  return Result<void>::Success();
}

template <CanPort Port>
Result<void> Client<Port>::idleFor(uint32_t us) noexcept
  requires CanRxPort<Port>
{
  clearPending();
  const uint32_t start = port_.NowUs();
  uint32_t frames = 0U;
  for (uint32_t reads = 0U; reads < kMaxReceivesPerWait; ++reads) {
    const uint32_t elapsed = port_.NowUs() - start;
    if ((elapsed >= us) || (frames >= kMaxFramesPerWait)) {
      break;
    }
    const RxStatus status = port_.Receive(rx_, us - elapsed);
    if (status == RxStatus::Error) {
      return Result<void>::Failure(Error::Bus);
    }
    if (status == RxStatus::Frame) {
      static_cast<void>(route(rx_));
      ++frames;
    }
  }
  return Result<void>::Success();
}

template <CanPort Port>
Result<FrameClass> Client<Port>::Poll(uint32_t timeout_us) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<FrameClass>::Failure(Error::NotInitialized);
  }
  clearPending();
  const RxStatus status = port_.Receive(rx_, timeout_us);
  if (status == RxStatus::Error) {
    return Result<FrameClass>::Failure(Error::Bus);
  }
  if (status == RxStatus::Timeout) {
    return Result<FrameClass>::Success(FrameClass::None);
  }
  return Result<FrameClass>::Success(route(rx_));
}

// --- Heartbeat, guarding, EMCY, TPDO
// ----------------------------------------------------------------

template <CanPort Port>
Result<void> Client<Port>::MonitorNode(uint8_t node, uint32_t timeout_us) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!IsNodeId(node) || (timeout_us < 1'000U) || (timeout_us > kMaxSdoTimeoutUs)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  detail::MonitorSlot* slot = findSlot(node);
  if (slot == nullptr) {
    for (detail::MonitorSlot& candidate : monitors_) {
      if (!candidate.used) {
        slot = &candidate;
        break;
      }
    }
    if (slot == nullptr) {
      return Result<void>::Failure(Error::BufferTooSmall);
    }
    *slot = detail::MonitorSlot{};
    slot->used = true;
    slot->node = node;
  }
  slot->timeout_us = timeout_us;
  return Result<void>::Success();
}

template <CanPort Port>
Result<void> Client<Port>::UnmonitorNode(uint8_t node) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  detail::MonitorSlot* slot = findSlot(node);
  if (slot == nullptr) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  *slot = detail::MonitorSlot{};
  return Result<void>::Success();
}

template <CanPort Port>
Result<NodeStatus> Client<Port>::GetNodeStatus(uint8_t node) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<NodeStatus>::Failure(Error::NotInitialized);
  }
  const detail::MonitorSlot* slot = findSlot(node);
  if (slot == nullptr) {
    return Result<NodeStatus>::Failure(Error::InvalidArgument);
  }
  NodeStatus status = slot->status;
  status.alive = status.seen && ((port_.NowUs() - status.last_us) <= slot->timeout_us);
  return Result<NodeStatus>::Success(status);
}

template <CanPort Port>
Result<NmtState> Client<Port>::GuardNode(uint8_t node) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<NmtState>::Failure(Error::NotInitialized);
  }
  const auto frame = MakeGuardRequest(node);
  if (!frame || (findSlot(node) == nullptr)) {
    return Result<NmtState>::Failure(Error::InvalidArgument);
  }
  if (const auto d = drainRx(); !d) {
    return Result<NmtState>::Failure(d.error);
  }
  pending_ = detail::Pending{};
  pending_.kind = detail::PendingKind::Guard;
  pending_.node = node;
  if (const auto s = sendFrame(frame.value); !s) {
    return Result<NmtState>::Failure(s.error);
  }
  if (const auto w = awaitPending(config_.guard_timeout_us); !w) {
    return Result<NmtState>::Failure(w.error);
  }
  return Result<NmtState>::Success(pending_.state);
}

template <CanPort Port>
bool Client<Port>::PopEmcy(EmcyEvent& event) noexcept
  requires CanRxPort<Port>
{
  if (emcy_count_ == 0U) {
    return false;
  }
  event = emcy_[emcy_head_];
  emcy_head_ = (emcy_head_ + 1U) % emcy_.size();
  --emcy_count_;
  return true;
}

template <CanPort Port>
Result<void> Client<Port>::SubscribeTpdo(uint16_t cob_id, uint8_t dlc) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if ((cob_id < kPdoCobIdMin) || (cob_id > kPdoCobIdMax) || (dlc < 1U) || (dlc > kMaxDlc)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  detail::PdoSlot* free_slot = nullptr;
  for (detail::PdoSlot& slot : pdos_) {
    if (slot.used && (slot.sample.cob_id == cob_id)) {
      slot.dlc = dlc; // subscribing again changes the expected length, keeps the sample
      return Result<void>::Success();
    }
    if (!slot.used && (free_slot == nullptr)) {
      free_slot = &slot;
    }
  }
  if (free_slot == nullptr) {
    return Result<void>::Failure(Error::BufferTooSmall);
  }
  *free_slot = detail::PdoSlot{};
  free_slot->used = true;
  free_slot->dlc = dlc;
  free_slot->sample.cob_id = cob_id;
  return Result<void>::Success();
}

template <CanPort Port>
Result<void> Client<Port>::UnsubscribeTpdo(uint16_t cob_id) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  for (detail::PdoSlot& slot : pdos_) {
    if (slot.used && (slot.sample.cob_id == cob_id)) {
      slot = detail::PdoSlot{};
      return Result<void>::Success();
    }
  }
  return Result<void>::Failure(Error::InvalidArgument);
}

template <CanPort Port>
Result<PdoSample> Client<Port>::ReadTpdo(uint16_t cob_id) const noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<PdoSample>::Failure(Error::NotInitialized);
  }
  for (const detail::PdoSlot& slot : pdos_) {
    if (slot.used && (slot.sample.cob_id == cob_id)) {
      return Result<PdoSample>::Success(slot.sample);
    }
  }
  return Result<PdoSample>::Failure(Error::InvalidArgument);
}

// --- SYNC producer with a period
// -----------------------------------------------------------------------

template <CanPort Port>
Result<void> Client<Port>::ConfigureSync(uint32_t period_us, uint8_t counter_overflow) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const bool period_ok =
      (period_us == 0U) || ((period_us >= 100U) && (period_us <= kMaxSdoTimeoutUs));
  const bool overflow_ok =
      (counter_overflow == 0U) || ((counter_overflow >= 2U) && (counter_overflow <= 240U));
  if (!period_ok || !overflow_ok) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  sync_period_us_ = period_us;
  sync_overflow_ = counter_overflow;
  sync_counter_ = 0U;
  sync_started_ = false;
  return Result<void>::Success();
}

template <CanPort Port>
Result<bool> Client<Port>::ServiceSync() noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<bool>::Failure(Error::NotInitialized);
  }
  if (sync_period_us_ == 0U) {
    return Result<bool>::Success(false);
  }
  const uint32_t now = port_.NowUs();
  if (sync_started_ && ((now - sync_last_us_) < sync_period_us_)) {
    return Result<bool>::Success(false);
  }
  uint8_t counter = 0U;
  if (sync_overflow_ != 0U) {
    counter = ((sync_counter_ == 0U) || (sync_counter_ >= sync_overflow_))
                  ? uint8_t{1}
                  : static_cast<uint8_t>(detail::Widen(sync_counter_) + 1U);
  }
  const auto frame = MakeSyncFrame(counter);
  if (!frame) {
    return Result<bool>::Failure(frame.error);
  }
  if (const auto sent = sendFrame(frame.value); !sent) {
    return Result<bool>::Failure(sent.error);
  }
  sync_counter_ = counter;
  sync_last_us_ = now;
  sync_started_ = true;
  return Result<bool>::Success(true);
}

// --- SDO client
// ----------------------------------------------------------------------------------------

template <CanPort Port>
Result<void> Client<Port>::sdoBegin(uint8_t node, uint16_t index, uint8_t sub, bool gated) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!IsNodeId(node)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (gated) {
    // A node whose heartbeat says Stopped ignores SDO: say so instead of waiting for a timeout.
    const detail::MonitorSlot* slot = findSlot(node);
    if ((slot != nullptr) && slot->status.seen && (slot->status.state == NmtState::Stopped) &&
        ((port_.NowUs() - slot->status.last_us) <= slot->timeout_us)) {
      return Result<void>::Failure(Error::NodeState);
    }
  }
  if (const auto drained = drainRx(); !drained && gated) {
    return drained; // a stale reply that was already queued is routed and dropped here
  }
  // The safety path (gated false) goes on when the port's receive side failed: the receive queue is
  // dropped by `drainRx()` and the request is sent anyway; the wait then reports the failure.
  pending_ = detail::Pending{};
  pending_.kind = detail::PendingKind::Sdo;
  pending_.node = node;
  pending_.index = index;
  pending_.sub = sub;
  return Result<void>::Success();
}

template <CanPort Port>
void Client<Port>::abortLocal(uint32_t code) noexcept {
  last_abort_ = SdoAbortInfo{.node = pending_.node,
                             .index = pending_.index,
                             .sub = pending_.sub,
                             .code = code,
                             .remote = false};
  const auto frame = MakeSdoAbort(pending_.node, pending_.index, pending_.sub, code);
  if (frame) {
    static_cast<void>(sendFrame(frame.value)); // best effort: the transfer has failed already
  }
}

template <CanPort Port>
Result<void> Client<Port>::sdoExchange(const CanFrame& request, SdoReplyKind expect,
                                       bool toggle) noexcept
  requires CanRxPort<Port>
{
  pending_.expect = expect;
  pending_.toggle = toggle;
  pending_.done = false;
  pending_.error = Error::Ok;
  pending_.abort_code = 0U;
  if (const auto sent = sendFrame(request); !sent) {
    return sent;
  }
  const auto waited = awaitPending(config_.sdo_timeout_us);
  if (waited) {
    return waited;
  }
  if ((waited.error == Error::Timeout) && config_.abort_on_timeout) {
    abortLocal(static_cast<uint32_t>(SdoAbortCode::Timeout));
  } else if ((waited.error == Error::Protocol) && (pending_.abort_code != 0U)) {
    abortLocal(pending_.abort_code);
  } else {
    // A bus failure, or the node aborted: nothing to tell the node.
  }
  return waited;
}

template <CanPort Port>
Result<bool> Client<Port>::uploadSegment(std::span<uint8_t> out, std::size_t& got,
                                         bool toggle) noexcept
  requires CanRxPort<Port>
{
  const auto request = MakeSdoUploadSegmentRequest(pending_.node, toggle);
  if (const auto x = sdoExchange(request.value, SdoReplyKind::SegmentUpload, toggle); !x) {
    return Result<bool>::Failure(x.error);
  }
  const SdoReply& segment = pending_.reply;
  if (segment.data_len > (out.size() - got)) {
    abortLocal(static_cast<uint32_t>(SdoAbortCode::OutOfMemory));
    return Result<bool>::Failure(Error::BufferTooSmall);
  }
  if ((segment.data_len == 0U) && !segment.last) {
    abortLocal(static_cast<uint32_t>(SdoAbortCode::InvalidCommandSpecifier));
    return Result<bool>::Failure(Error::Protocol);
  }
  for (std::size_t i = 0; i < segment.data_len; ++i) {
    out[got + i] = segment.data[i];
  }
  got += segment.data_len;
  return Result<bool>::Success(segment.last);
}

template <CanPort Port>
Result<std::size_t> Client<Port>::uploadSegments(std::span<uint8_t> out) noexcept
  requires CanRxPort<Port>
{
  const bool sized = pending_.reply.size_indicated;
  const uint32_t announced = pending_.reply.size;
  std::size_t got = 0U;
  bool toggle = false;
  for (uint32_t i = 0U; i < kMaxSdoSegments; ++i) {
    const auto step = uploadSegment(out, got, toggle);
    if (!step) {
      return Result<std::size_t>::Failure(step.error);
    }
    if (step.value) {
      if (sized && (got != announced)) {
        abortLocal(static_cast<uint32_t>(SdoAbortCode::LengthMismatch));
        return Result<std::size_t>::Failure(Error::Protocol);
      }
      return Result<std::size_t>::Success(got);
    }
    toggle = !toggle;
  }
  abortLocal(static_cast<uint32_t>(SdoAbortCode::OutOfMemory));
  return Result<std::size_t>::Failure(Error::Protocol);
}

template <CanPort Port>
Result<std::size_t> Client<Port>::SdoUpload(uint8_t node, uint16_t index, uint8_t sub,
                                            std::span<uint8_t> out) noexcept
  requires CanRxPort<Port>
{
  if (const auto b = sdoBegin(node, index, sub, true); !b) {
    return Result<std::size_t>::Failure(b.error);
  }
  const auto request = MakeSdoUploadRequest(node, index, sub);
  if (const auto x = sdoExchange(request.value, SdoReplyKind::InitiateUpload, false); !x) {
    return Result<std::size_t>::Failure(x.error);
  }
  const SdoReply& first = pending_.reply;
  if (first.expedited) {
    if (first.data_len > out.size()) {
      return Result<std::size_t>::Failure(Error::BufferTooSmall); // the transfer is complete
    }
    for (std::size_t i = 0; i < first.data_len; ++i) {
      out[i] = first.data[i];
    }
    return Result<std::size_t>::Success(first.data_len);
  }
  if (first.size_indicated && (first.size > out.size())) {
    abortLocal(static_cast<uint32_t>(SdoAbortCode::OutOfMemory));
    return Result<std::size_t>::Failure(Error::BufferTooSmall);
  }
  return uploadSegments(out);
}

template <CanPort Port>
Result<void> Client<Port>::downloadSegments(std::span<const uint8_t> data) noexcept
  requires CanRxPort<Port>
{
  std::size_t offset = 0U;
  bool toggle = false;
  for (uint32_t i = 0U; i < kMaxSdoSegments; ++i) {
    const std::size_t length = ((data.size() - offset) > 7U) ? 7U : (data.size() - offset);
    const bool last = (offset + length) == data.size();
    const auto frame =
        MakeSdoDownloadSegment(pending_.node, toggle, data.subspan(offset, length), last);
    if (const auto x = sdoExchange(frame.value, SdoReplyKind::SegmentDownload, toggle); !x) {
      return x;
    }
    if (last) {
      return Result<void>::Success();
    }
    offset += length;
    toggle = !toggle;
  }
  return Result<void>::Failure(
      Error::InvalidArgument); // unreachable: SdoDownload() bounds the size
}

template <CanPort Port>
Result<void> Client<Port>::SdoDownload(uint8_t node, uint16_t index, uint8_t sub,
                                       std::span<const uint8_t> data) noexcept
  requires CanRxPort<Port>
{
  constexpr std::size_t kMaxSegmentedBytes = static_cast<std::size_t>(kMaxSdoSegments) * 7U;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (data.empty() || (data.size() > kMaxSegmentedBytes)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (const auto b = sdoBegin(node, index, sub, true); !b) {
    return b;
  }
  if (data.size() <= 4U) {
    const auto frame = MakeSdoExpeditedDownload(node, index, sub, data);
    return sdoExchange(frame.value, SdoReplyKind::InitiateDownload, false);
  }
  const auto frame = MakeSdoSegmentedDownload(node, index, sub, static_cast<uint32_t>(data.size()));
  if (const auto x = sdoExchange(frame.value, SdoReplyKind::InitiateDownload, false); !x) {
    return x;
  }
  return downloadSegments(data);
}

template <CanPort Port>
Result<uint32_t> Client<Port>::readUnsigned(uint8_t node, uint16_t index, uint8_t sub,
                                            std::size_t size) noexcept
  requires CanRxPort<Port>
{
  std::array<uint8_t, 4> buffer{};
  const auto read = SdoUpload(node, index, sub, std::span<uint8_t>(buffer));
  if (!read) {
    // An object larger than the type is a protocol mismatch, not a small buffer.
    return Result<uint32_t>::Failure((read.error == Error::BufferTooSmall) ? Error::Protocol
                                                                           : read.error);
  }
  if (read.value != size) {
    return Result<uint32_t>::Failure(Error::Protocol);
  }
  uint32_t value = 0U;
  for (std::size_t i = 0; i < size; ++i) {
    value |= detail::Widen(buffer[i]) << (8U * static_cast<uint32_t>(i));
  }
  return Result<uint32_t>::Success(value);
}

template <CanPort Port>
Result<void> Client<Port>::writeUnsigned(uint8_t node, uint16_t index, uint8_t sub, uint32_t value,
                                         std::size_t size, bool gated) noexcept
  requires CanRxPort<Port>
{
  const std::array<uint8_t, 4> buffer{
      static_cast<uint8_t>(value & 0xFFU), static_cast<uint8_t>((value >> 8U) & 0xFFU),
      static_cast<uint8_t>((value >> 16U) & 0xFFU), static_cast<uint8_t>((value >> 24U) & 0xFFU)};
  const auto frame =
      MakeSdoExpeditedDownload(node, index, sub, std::span<const uint8_t>(buffer).first(size));
  if (const auto b = sdoBegin(node, index, sub, gated); !b) {
    return b;
  }
  if (!frame) {
    return Result<void>::Failure(frame.error);
  }
  return sdoExchange(frame.value, SdoReplyKind::InitiateDownload, false);
}

template <CanPort Port>
Result<uint8_t> Client<Port>::SdoReadU8(uint8_t node, uint16_t index, uint8_t sub) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  const auto r = readUnsigned(node, index, sub, 1_z);
  return r ? Result<uint8_t>::Success(static_cast<uint8_t>(r.value))
           : Result<uint8_t>::Failure(r.error);
}

template <CanPort Port>
Result<uint16_t> Client<Port>::SdoReadU16(uint8_t node, uint16_t index, uint8_t sub) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  const auto r = readUnsigned(node, index, sub, 2_z);
  return r ? Result<uint16_t>::Success(static_cast<uint16_t>(r.value))
           : Result<uint16_t>::Failure(r.error);
}

template <CanPort Port>
Result<uint32_t> Client<Port>::SdoReadU32(uint8_t node, uint16_t index, uint8_t sub) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  return readUnsigned(node, index, sub, 4_z);
}

template <CanPort Port>
Result<void> Client<Port>::SdoWriteU8(uint8_t node, uint16_t index, uint8_t sub,
                                      uint8_t value) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  return writeUnsigned(node, index, sub, value, 1_z, true);
}

template <CanPort Port>
Result<void> Client<Port>::SdoWriteU16(uint8_t node, uint16_t index, uint8_t sub,
                                       uint16_t value) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  return writeUnsigned(node, index, sub, value, 2_z, true);
}

template <CanPort Port>
Result<void> Client<Port>::SdoWriteU32(uint8_t node, uint16_t index, uint8_t sub,
                                       uint32_t value) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  return writeUnsigned(node, index, sub, value, 4_z, true);
}

// --- Communication objects of a node
// ------------------------------------------------------------------

template <CanPort Port>
Result<uint32_t> Client<Port>::ReadDeviceType(uint8_t node) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  using detail::operator""_u16;
  return SdoReadU32(node, 0x1000_u16, 0_u8);
}

template <CanPort Port>
Result<uint8_t> Client<Port>::ReadErrorRegister(uint8_t node) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  using detail::operator""_u16;
  return SdoReadU8(node, 0x1001_u16, 0_u8);
}

template <CanPort Port>
Result<void> Client<Port>::WriteHeartbeatProducerTime(uint8_t node, uint16_t period_ms) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  using detail::operator""_u16;
  return SdoWriteU16(node, 0x1017_u16, 0_u8, period_ms);
}

template <CanPort Port>
Result<void> Client<Port>::storeOrRestore(uint8_t node, uint16_t index, uint8_t group,
                                          uint32_t signature) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if ((group < 1U) || (group > 4U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return SdoWriteU32(node, index, group, signature);
}

template <CanPort Port>
Result<void> Client<Port>::StoreParameters(uint8_t node, uint8_t group) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u16;
  using detail::operator""_u32;
  return storeOrRestore(node, 0x1010_u16, group, 0x65766173_u32); // "save"
}

template <CanPort Port>
Result<void> Client<Port>::RestoreDefaults(uint8_t node, uint8_t group) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u16;
  using detail::operator""_u32;
  return storeOrRestore(node, 0x1011_u16, group, 0x64616F6C_u32); // "load"
}

// --- PDO configuration
// -------------------------------------------------------------------------------------

template <CanPort Port>
Result<void> Client<Port>::writePdoCommunication(uint8_t node, PdoKind kind, uint16_t comm_index,
                                                 const PdoConfig& config) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  using detail::operator""_u32;
  constexpr uint32_t kNotValid = 0x80000000_u32;
  if (const auto r = SdoWriteU32(node, comm_index, 1_u8, detail::Widen(config.cob_id) | kNotValid);
      !r) {
    return r;
  }
  if (const auto r = SdoWriteU8(node, comm_index, 2_u8, config.transmission_type); !r) {
    return r;
  }
  if (kind != PdoKind::Transmit) {
    return Result<void>::Success();
  }
  if (config.inhibit_time_100us != 0U) {
    if (const auto r = SdoWriteU16(node, comm_index, 3_u8, config.inhibit_time_100us); !r) {
      return r;
    }
  }
  if (config.event_timer_ms != 0U) {
    return SdoWriteU16(node, comm_index, 5_u8, config.event_timer_ms);
  }
  return Result<void>::Success();
}

template <CanPort Port>
Result<void> Client<Port>::writePdoMapping(uint8_t node, uint16_t map_index,
                                           const PdoConfig& config) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  if (const auto r = SdoWriteU8(node, map_index, 0_u8, 0_u8); !r) {
    return r;
  }
  for (std::size_t i = 0; i < config.entry_count; ++i) {
    const auto sub = static_cast<uint8_t>(i + 1U);
    if (const auto r = SdoWriteU32(node, map_index, sub, EncodePdoMapEntry(config.entries[i]));
        !r) {
      return r;
    }
  }
  return SdoWriteU8(node, map_index, 0_u8, config.entry_count);
}

template <CanPort Port>
Result<void> Client<Port>::ConfigurePdo(uint8_t node, PdoKind kind, uint16_t number,
                                        const PdoConfig& config) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto comm = PdoCommunicationIndex(kind, number);
  const auto map = PdoMappingIndex(kind, number);
  if (!IsNodeId(node) || !comm || !map || (ValidatePdoConfig(config) != Error::Ok) ||
      (config.entry_count == 0U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (const auto r = writePdoCommunication(node, kind, comm.value, config); !r) {
    return r;
  }
  if (const auto r = writePdoMapping(node, map.value, config); !r) {
    return r;
  }
  if (!config.enabled) {
    return Result<void>::Success(); // left invalid, as asked
  }
  return SdoWriteU32(node, comm.value, 1_u8, detail::Widen(config.cob_id));
}

template <CanPort Port>
Result<void> Client<Port>::DisablePdo(uint8_t node, PdoKind kind, uint16_t number) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  using detail::operator""_u32;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  const auto comm = PdoCommunicationIndex(kind, number);
  if (!comm) {
    return Result<void>::Failure(comm.error);
  }
  const auto id = SdoReadU32(node, comm.value, 1_u8);
  if (!id) {
    return Result<void>::Failure(id.error);
  }
  return SdoWriteU32(node, comm.value, 1_u8, id.value | 0x80000000_u32);
}

template <CanPort Port>
Result<PdoConfig> Client<Port>::ReadPdoConfig(uint8_t node, PdoKind kind, uint16_t number) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  using detail::operator""_u32;
  if (!initialized_) {
    return Result<PdoConfig>::Failure(Error::NotInitialized);
  }
  const auto comm = PdoCommunicationIndex(kind, number);
  const auto map = PdoMappingIndex(kind, number);
  if (!comm || !map) {
    return Result<PdoConfig>::Failure(Error::InvalidArgument);
  }
  const auto id = SdoReadU32(node, comm.value, 1_u8);
  if (!id) {
    return Result<PdoConfig>::Failure(id.error);
  }
  if ((id.value & 0x3FFFF800_u32) != 0U) { // bit 29 (29-bit identifier) or bits 11 to 28
    return Result<PdoConfig>::Failure(Error::Unsupported);
  }
  PdoConfig config{};
  config.cob_id = static_cast<uint16_t>(id.value & 0x7FFU);
  config.enabled = (id.value & 0x80000000_u32) == 0U;
  const auto type = SdoReadU8(node, comm.value, 2_u8);
  const auto count = SdoReadU8(node, map.value, 0_u8);
  if (!type || !count) {
    return Result<PdoConfig>::Failure(!type ? type.error : count.error);
  }
  if (count.value > kMaxPdoEntries) {
    return Result<PdoConfig>::Failure(Error::BufferTooSmall);
  }
  config.transmission_type = type.value;
  config.entry_count = count.value;
  for (std::size_t i = 0; i < config.entry_count; ++i) {
    const auto record = SdoReadU32(node, map.value, static_cast<uint8_t>(i + 1U));
    if (!record) {
      return Result<PdoConfig>::Failure(record.error);
    }
    config.entries[i] = DecodePdoMapEntry(record.value);
  }
  return Result<PdoConfig>::Success(config);
}

// --- CiA 402 drive
// ----------------------------------------------------------------------------------------

namespace detail {

/// A drive's object indices are application objects: at least 0x1000.
constexpr bool IsDriveObjects(const DriveObjects& o) noexcept {
  using detail::operator""_u16;
  return (o.controlword >= 0x1000_u16) && (o.statusword >= 0x1000_u16) && (o.mode >= 0x1000_u16) &&
         (o.mode_display >= 0x1000_u16);
}

} // namespace detail

template <CanPort Port>
Result<uint16_t> Client<Port>::ReadStatusword(uint8_t node, const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  if (!detail::IsDriveObjects(objects)) {
    return Result<uint16_t>::Failure(Error::InvalidArgument);
  }
  return SdoReadU16(node, objects.statusword, 0_u8);
}

template <CanPort Port>
Result<DriveState> Client<Port>::ReadDriveState(uint8_t node, const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  const auto sw = ReadStatusword(node, objects);
  if (!sw) {
    return Result<DriveState>::Failure(sw.error);
  }
  return Result<DriveState>::Success(DecodeStatusword(sw.value));
}

template <CanPort Port>
bool Client<Port>::driveTimedOut(uint32_t start_us) noexcept
  requires CanRxPort<Port>
{
  return (port_.NowUs() - start_us) >= config_.drive_timeout_us;
}

template <CanPort Port>
uint32_t Client<Port>::driveStepLimit() const noexcept {
  // One iteration takes at least one poll pause, so this is the time bound as a count; it only
  // matters to a port whose clock or `Receive()` does not let time pass.
  return (config_.drive_timeout_us / config_.drive_poll_interval_us) + 2U;
}

template <CanPort Port>
Result<void> Client<Port>::DriveEnable(uint8_t node, const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!detail::IsDriveObjects(objects)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const uint32_t start = port_.NowUs();
  const uint32_t limit = driveStepLimit();
  DriveState written_in = DriveState::Unknown; // the state the last controlword was written in
  for (uint32_t i = 0U; i < limit; ++i) {
    const auto state = ReadDriveState(node, objects);
    if (!state) {
      return Result<void>::Failure(state.error);
    }
    if (state.value == DriveState::OperationEnabled) {
      return Result<void>::Success();
    }
    if ((state.value == DriveState::Fault) || (state.value == DriveState::Unknown)) {
      return Result<void>::Failure(Error::DriveFault);
    }
    // One command per state seen: the drive acts on it in its own cycle, and writing it again while
    // the statusword has not changed yet would only restart a slow drive's transition.
    uint16_t word = 0U;
    if ((state.value != written_in) && EnableStepControlword(state.value, word)) {
      if (const auto w = writeUnsigned(node, objects.controlword, 0_u8, word, 2_z, true); !w) {
        return w;
      }
      written_in = state.value;
    }
    if (driveTimedOut(start)) {
      return Result<void>::Failure(Error::Timeout);
    }
    if (const auto idle = idleFor(config_.drive_poll_interval_us); !idle) {
      return idle;
    }
  }
  return Result<void>::Failure(Error::Timeout);
}

template <CanPort Port>
Result<void> Client<Port>::safetyWriteControlword(uint8_t node, const DriveObjects& objects,
                                                  uint16_t word) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!detail::IsDriveObjects(objects)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // Not gated on the node's heartbeat state, and repeated until the node acknowledges it.
  Result<void> last = Result<void>::Failure(Error::Timeout);
  for (uint8_t attempt = 0U; attempt < config_.safety_attempts; ++attempt) {
    last = writeUnsigned(node, objects.controlword, 0_u8, word, 2_z, false);
    if (last || (last.error == Error::NotInitialized) || (last.error == Error::InvalidArgument)) {
      break;
    }
  }
  return last;
}

template <CanPort Port>
Result<void> Client<Port>::DriveDisable(uint8_t node, const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  return safetyWriteControlword(node, objects, kControlDisableVoltage);
}

template <CanPort Port>
Result<void> Client<Port>::DriveQuickStop(uint8_t node, const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  return safetyWriteControlword(node, objects, kControlQuickStop);
}

template <CanPort Port>
Result<void> Client<Port>::waitFaultCleared(uint8_t node, const DriveObjects& objects,
                                            uint32_t start) noexcept
  requires CanRxPort<Port>
{
  const uint32_t limit = driveStepLimit();
  for (uint32_t i = 0U; i < limit; ++i) {
    if (const auto idle = idleFor(config_.drive_poll_interval_us); !idle) {
      return idle;
    }
    const auto state = ReadDriveState(node, objects);
    if (!state) {
      return Result<void>::Failure(state.error);
    }
    if ((state.value != DriveState::Fault) && (state.value != DriveState::FaultReactionActive)) {
      return Result<void>::Success();
    }
    if (driveTimedOut(start)) {
      break;
    }
  }
  return Result<void>::Failure(Error::DriveFault);
}

template <CanPort Port>
Result<void> Client<Port>::DriveResetFault(uint8_t node, const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!detail::IsDriveObjects(objects)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const uint32_t start = port_.NowUs();
  const uint32_t limit = driveStepLimit();
  for (uint32_t i = 0U; i < limit; ++i) {
    const auto state = ReadDriveState(node, objects);
    if (!state) {
      return Result<void>::Failure(state.error);
    }
    if (state.value == DriveState::Fault) {
      // Bit 7 acts on the rising edge: clear it first, then set it.
      if (const auto w =
              writeUnsigned(node, objects.controlword, 0_u8, kControlDisableVoltage, 2_z, true);
          !w) {
        return w;
      }
      if (const auto w =
              writeUnsigned(node, objects.controlword, 0_u8, kControlFaultReset, 2_z, true);
          !w) {
        return w;
      }
      // One clock for both waits (the fault reaction and the clearing): `drive_timeout_us` in all.
      if (const auto cleared = waitFaultCleared(node, objects, start); !cleared) {
        return cleared;
      }
      // Bit 7 is left high by the reset: drop it, so a drive that reads it as a level does not
      // reset the next fault by itself. 0x0000 is Disable voltage, the state the drive is in.
      return writeUnsigned(node, objects.controlword, 0_u8, kControlDisableVoltage, 2_z, true);
    }
    if (state.value == DriveState::Unknown) {
      return Result<void>::Failure(Error::DriveFault); // a statusword that is no state
    }
    if (state.value != DriveState::FaultReactionActive) {
      return Result<void>::Success(); // not in a fault state: nothing to reset
    }
    if (driveTimedOut(start)) {
      break;
    }
    if (const auto idle = idleFor(config_.drive_poll_interval_us); !idle) {
      return idle;
    }
  }
  return Result<void>::Failure(Error::Timeout);
}

template <CanPort Port>
Result<void> Client<Port>::SetDriveMode(uint8_t node, OperationMode mode,
                                        const DriveObjects& objects, Verify verify) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_z;
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!detail::IsDriveObjects(objects)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const auto raw = static_cast<uint8_t>(mode);
  if (const auto w = writeUnsigned(node, objects.mode, 0_u8, raw, 1_z, true); !w) {
    return w;
  }
  if (verify != Verify::ReadBack) {
    return Result<void>::Success();
  }
  const uint32_t start = port_.NowUs();
  const uint32_t limit = driveStepLimit();
  for (uint32_t i = 0U; i < limit; ++i) {
    const auto shown = readUnsigned(node, objects.mode_display, 0_u8, 1_z);
    if (!shown) {
      return Result<void>::Failure(shown.error);
    }
    if (shown.value == detail::Widen(raw)) {
      return Result<void>::Success();
    }
    if (driveTimedOut(start)) {
      break;
    }
    if (const auto idle = idleFor(config_.drive_poll_interval_us); !idle) {
      return idle;
    }
  }
  return Result<void>::Failure(Error::DeviceFault);
}

template <CanPort Port>
Result<OperationMode> Client<Port>::ReadDriveMode(uint8_t node,
                                                  const DriveObjects& objects) noexcept
  requires CanRxPort<Port>
{
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<OperationMode>::Failure(Error::NotInitialized);
  }
  if (!detail::IsDriveObjects(objects)) {
    return Result<OperationMode>::Failure(Error::InvalidArgument);
  }
  const auto shown = SdoReadU8(node, objects.mode_display, 0_u8);
  if (!shown) {
    return Result<OperationMode>::Failure(shown.error);
  }
  return Result<OperationMode>::Success(
      static_cast<OperationMode>(static_cast<int8_t>(shown.value)));
}

// --- Raw access (diagnostics)
// -----------------------------------------------------------------------

template <CanPort Port>
Result<void> Client<Port>::SendRawFrame(const CanFrame& frame) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!IsValid(frame)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return sendFrame(frame);
}

template <CanPort Port>
Result<CanFrame> Client<Port>::ReadRawFrame(uint32_t timeout_us) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<CanFrame>::Failure(Error::NotInitialized);
  }
  clearPending();
  const RxStatus status = port_.Receive(rx_, timeout_us);
  if (status == RxStatus::Error) {
    return Result<CanFrame>::Failure(Error::Bus);
  }
  if (status != RxStatus::Frame) {
    return Result<CanFrame>::Failure(Error::Timeout);
  }
  last_raw_ = rx_;
  return Result<CanFrame>::Success(last_raw_);
}

template <CanPort Port>
Result<CanFrame> Client<Port>::rawExchange(uint8_t node, const CanFrame& request) noexcept
  requires CanRxPort<Port>
{
  // A reply that was already queued belongs to an earlier request: route and drop it, so the first
  // frame returned answers this one. A failing receive side is not a reason to refuse here; the
  // wait reports it.
  static_cast<void>(drainRx());
  pending_ = detail::Pending{};
  pending_.kind = detail::PendingKind::RawSdo;
  pending_.node = node;
  if (const auto sent = sendFrame(request); !sent) {
    clearPending();
    return Result<CanFrame>::Failure(sent.error);
  }
  const auto waited = awaitPending(config_.sdo_timeout_us);
  clearPending();
  if (!waited) {
    return Result<CanFrame>::Failure(waited.error);
  }
  return Result<CanFrame>::Success(last_raw_);
}

template <CanPort Port>
Result<CanFrame> Client<Port>::ReadRawSdo(uint8_t node, uint16_t index, uint8_t sub) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<CanFrame>::Failure(Error::NotInitialized);
  }
  const auto request = MakeSdoUploadRequest(node, index, sub);
  if (!request) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  return rawExchange(node, request.value);
}

template <CanPort Port>
Result<CanFrame> Client<Port>::WriteRawSdo(uint8_t node, uint16_t index, uint8_t sub,
                                           std::span<const uint8_t> data) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<CanFrame>::Failure(Error::NotInitialized);
  }
  const auto request = MakeSdoExpeditedDownload(node, index, sub, data);
  if (!request) {
    return Result<CanFrame>::Failure(Error::InvalidArgument);
  }
  return rawExchange(node, request.value);
}

template <CanPort Port>
Result<void> Client<Port>::ClearBootUp(uint8_t node) noexcept
  requires CanRxPort<Port>
{
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  detail::MonitorSlot* slot = findSlot(node);
  if (slot == nullptr) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  slot->status.bootup_seen = false;
  return Result<void>::Success();
}

} // namespace canopen_client

#endif // CANOPEN_CLIENT_IPP

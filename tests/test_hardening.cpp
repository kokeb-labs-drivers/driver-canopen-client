/**
 * @file test_hardening.cpp
 * @brief Safety-path, bound and reply-validity tests: each one fails on a one-edit mutant of the
 * code it pins.
 */
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using canopen_client::FrameClass;
using canopen_client::NmtCommand;
using canopen_client::NmtState;
using canopen_client::PdoConfig;
using canopen_client::PdoKind;
using canopen_client::RxStatus;
using namespace canopen_client_test;

namespace {

canopen_client::Config Quick() {
  canopen_client::Config c{};
  c.drive_poll_interval_us = 1'000;
  c.drive_timeout_us = 200'000;
  c.sdo_timeout_us = 20'000;
  c.abort_on_timeout = false;
  return c;
}

void Put(SimNode& n, SimDrive::State s) {
  n.drive.state = s;
  n.drive.pending = s;
}

/// Frames the client put on the wire to `id` since `mark`.
std::size_t Count(const Rig& r, std::size_t mark, uint16_t id) {
  return r.SentWithId(mark, id);
}

} // namespace

// ---- raw access ---------------------------------------------------------------------------

TEST_CASE("SendRawFrame sends the frame as given, once, and refuses what is not a frame") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  CHECK(r.client.SendRawFrame(Frame(0x123, {1, 2, 3})).ok());
  REQUIRE(r.Sent() == mark + 1);
  CHECK(r.Bus().sent.back().id == 0x123 && r.Bus().sent.back().dlc == 3);
  CanFrame bad = Frame(0x123, {1});
  bad.id = 0x800;
  CHECK(r.client.SendRawFrame(bad).error == Error::InvalidArgument);
  bad = Frame(0x123, {1});
  bad.dlc = 9;
  CHECK(r.client.SendRawFrame(bad).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark + 1);
  r.Bus().send_ok = false;
  CHECK(r.client.SendRawFrame(Frame(0x123, {1})).error == Error::Bus);
  canopen_client::Client<MockPort> cold(r.port);
  CHECK(cold.SendRawFrame(Frame(0x123, {1})).error == Error::NotInitialized);
}

TEST_CASE("ReadRawFrame returns the next frame untouched and routes nothing") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 10);
  const auto f = r.client.ReadRawFrame(1'000);
  REQUIRE(f.ok());
  CHECK(f.value.id == 0x705 && f.value.data[0] == 0x05);
  CHECK(r.client.GetLastRawFrame().id == 0x705);
  CHECK(!r.client.GetNodeStatus(5).value.seen); // the heartbeat was not routed
  CHECK(r.client.ReadRawFrame(1'000).error == Error::Timeout);
  r.Bus().receive_error = true;
  CHECK(r.client.ReadRawFrame(1'000).error == Error::Bus);
  canopen_client::Client<MockPort> cold(r.port);
  CHECK(cold.ReadRawFrame(0).error == Error::NotInitialized);
}

TEST_CASE("ReadRawSdo sends one request and returns the first reply as received") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  const auto reply = r.client.ReadRawSdo(5, 0x1000, 0);
  REQUIRE(reply.ok());
  CHECK_EQ(Count(r, mark, 0x605), 1U);
  CHECK(reply.value.id == 0x585 && reply.value.data[0] == 0x43);
  CHECK_EQ(reply.value.data[4], 0x92);
  CHECK(r.client.GetLastRawFrame().id == 0x585);
  // An abort is a reply too: handed back, not turned into an error.
  const auto abort = r.client.ReadRawSdo(5, 0x7777, 0);
  REQUIRE(abort.ok());
  CHECK_EQ(abort.value.data[0], 0x80);
}

TEST_CASE("A raw SDO exchange has no retry, no abort and no gate") {
  canopen_client::Config c = Quick();
  c.abort_on_timeout = true; // a normal SDO call would send an abort after a timeout
  Rig r(c);
  REQUIRE(r.started);
  r.n5.responds = false;
  const std::size_t mark = r.Sent();
  CHECK(r.client.ReadRawSdo(5, 0x1000, 0).error == Error::Timeout);
  CHECK_EQ(r.Sent(), mark + 1); // the request only: no second try, no abort frame
  // A node known to be Stopped is still asked: a diagnostic shows what the node does.
  REQUIRE(r.client.MonitorNode(6, 1'000'000).ok());
  r.Bus().Inject(Frame(0x706, {0x04}), r.Bus().now_us + 10);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  CHECK(r.client.SdoReadU32(6, 0x1000, 0).error == Error::NodeState);
  CHECK(r.client.ReadRawSdo(6, 0x1000, 0).ok());
}

TEST_CASE("ReadRawSdo and WriteRawSdo refuse bad arguments without sending") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  const std::array<uint8_t, 5> five{};
  CHECK(r.client.ReadRawSdo(0, 0x1000, 0).error == Error::InvalidArgument);
  CHECK(r.client.ReadRawSdo(128, 0x1000, 0).error == Error::InvalidArgument);
  CHECK(r.client.WriteRawSdo(5, 0x2001, 0, std::span<const uint8_t>()).error ==
        Error::InvalidArgument);
  CHECK(r.client.WriteRawSdo(5, 0x2001, 0, five).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
  canopen_client::Client<MockPort> cold(r.port);
  CHECK(cold.ReadRawSdo(5, 0x1000, 0).error == Error::NotInitialized);
  CHECK(cold.WriteRawSdo(5, 0x2001, 0, five).error == Error::NotInitialized);
  r.Bus().send_ok = false;
  CHECK(r.client.ReadRawSdo(5, 0x1000, 0).error == Error::Bus);
}

TEST_CASE("WriteRawSdo writes and returns the download response") {
  Rig r;
  REQUIRE(r.started);
  const std::array<uint8_t, 2> v{0x34, 0x12};
  const auto reply = r.client.WriteRawSdo(5, 0x2001, 0, v);
  REQUIRE(reply.ok());
  CHECK_EQ(reply.value.data[0], 0x60);
  CHECK_EQ(r.n5.Value(0x2001, 0), 0x1234U);
}

// ---- the safety path ------------------------------------------------------------------------

TEST_CASE("DriveDisable and DriveQuickStop send even when the port's receive side fails") {
  Rig r;
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Enabled);
  r.Bus().receive_error = true;
  std::size_t mark = r.Sent();
  const auto d = r.client.DriveDisable(5);
  CHECK(d.error == Error::Bus);        // reported afterwards
  CHECK_EQ(Count(r, mark, 0x605), 3U); // all safety_attempts went out
  CHECK_EQ(r.n5.drive.controlword, 0x0000);
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
  Put(r.n5, SimDrive::State::Enabled);
  mark = r.Sent();
  CHECK(r.client.DriveQuickStop(5).error == Error::Bus);
  CHECK_EQ(Count(r, mark, 0x605), 3U);
  CHECK_EQ(r.n5.drive.controlword, 0x0002);
  CHECK(r.n5.drive.state == SimDrive::State::QuickStop);
}

TEST_CASE("An ordinary SDO call still sends nothing when the receive side fails") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().receive_error = true;
  const std::size_t mark = r.Sent();
  CHECK(r.client.SdoReadU32(5, 0x1000, 0).error == Error::Bus);
  CHECK(r.client.SdoWriteU16(5, 0x2001, 0, 1).error == Error::Bus);
  CHECK_EQ(r.Sent(), mark);
}

TEST_CASE(
    "The stop commands go out on a Stopped node and after a send failure on the first attempt") {
  Rig r;
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Enabled);
  REQUIRE(r.client.MonitorNode(5, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x04}), r.Bus().now_us + 10);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  r.Bus().fail_send_index = r.Bus().sends; // the first attempt is refused by the port
  CHECK(r.client.DriveDisable(5).ok());    // the second one went through
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
}

// ---- drive waits are bounded by time ---------------------------------------------------------

TEST_CASE("DriveEnable waits for slow transitions as long as drive_timeout_us allows") {
  canopen_client::Config c{};
  c.drive_timeout_us = 2'000'000;
  Rig r(c); // poll interval 10 ms
  REQUIRE(r.started);
  r.n5.drive.transition_delay_us = 300'000; // three transitions, 0.9 s in all
  CHECK(r.client.DriveEnable(5).ok());
  CHECK(r.n5.drive.state == SimDrive::State::Enabled);
}

TEST_CASE("A tiny poll interval does not cut the wait short") {
  canopen_client::Config c{};
  c.drive_poll_interval_us = 100;
  c.drive_timeout_us = 60'000'000;
  c.sdo_timeout_us = 20'000;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.drive.transition_delay_us = 50'000;
  const uint64_t t0 = r.Bus().now_us;
  CHECK(r.client.DriveEnable(5).ok());
  CHECK(r.Bus().now_us - t0 >= 150'000U);
}

TEST_CASE("DriveEnable gives up after drive_timeout_us, not before") {
  canopen_client::Config c{};
  c.drive_poll_interval_us = 100;
  c.drive_timeout_us = 1'000'000;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.drive.transition_delay_us = 5'000'000;
  const uint64_t t0 = r.Bus().now_us;
  const auto e = r.client.DriveEnable(5);
  CHECK(e.error == Error::Timeout);
  CHECK(r.Bus().now_us - t0 >= 1'000'000U);
  CHECK(r.Bus().now_us - t0 < 1'100'000U);
}

TEST_CASE("DriveResetFault and SetDriveMode read-back wait by time too") {
  canopen_client::Config c{};
  c.drive_poll_interval_us = 100;
  c.drive_timeout_us = 5'000'000;
  c.sdo_timeout_us = 20'000;
  Rig r(c);
  REQUIRE(r.started);
  // Fault reaction takes 300 ms, then the reset takes 300 ms to show.
  r.n5.drive.fault_reaction_us = 300'000;
  r.n5.drive.transition_delay_us = 300'000;
  r.n5.drive.RaiseFault(r.Bus().now_us);
  CHECK(r.client.DriveResetFault(5).ok());
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
  r.n5.drive.mode_latency_us = 300'000;
  CHECK(r.client
            .SetDriveMode(5, canopen_client::OperationMode::ProfileVelocity, {},
                          canopen_client::Verify::ReadBack)
            .ok());
}

TEST_CASE("DriveResetFault drops bit 7 before it raises it") {
  Rig r;
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Fault);
  r.n5.drive.controlword =
      0x0080; // left set by an earlier reset attempt: no edge unless it is cleared
  CHECK(r.client.DriveResetFault(5).ok());
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
}

TEST_CASE(
    "DriveResetFault reports a statusword that is no state as a fault, a healthy drive is left") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.DriveResetFault(5).ok()); // Switch on disabled: nothing to reset
  CHECK_EQ(r.n5.drive.writes, 0);
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585 && f.data[1] == 0x41 && f.data[2] == 0x60) {
      f.data[4] = 0x0B; // matches no state
      f.data[5] = 0x00;
    }
  };
  const auto e = r.client.DriveResetFault(5);
  CHECK(!e.ok() && e.error == Error::DriveFault);
  CHECK_EQ(r.n5.drive.writes, 0);
}

TEST_CASE("Frames that arrive during the drive's poll pauses are routed") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.drive.transition_delay_us = 20'000;
  for (uint64_t i = 0; i < 6;
       ++i) { // spread over the polls, some land in a pause and some in an exchange
    r.Bus().Inject(Frame(0x085, {0x10, 0x23, 0x02, 0, 0, 0, 0, 0}),
                   r.Bus().now_us + 5'000 + (i * 1'700));
  }
  CHECK(r.client.DriveEnable(5).ok());
  canopen_client::EmcyEvent e{};
  std::size_t popped = 0;
  while (r.client.PopEmcy(e)) {
    ++popped;
  }
  CHECK_EQ(popped, 6U);
}

// ---- the wait budget counts frames, not early returns -----------------------------------------

namespace {

/// A port whose Receive returns at once with no frame, as one with a non-blocking poll does.
class EarlyPort : public canopen_client::CanInterface<EarlyPort> {
public:
  uint32_t now_us = 0;
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Send(const CanFrame& frame) noexcept {
    static_cast<void>(frame);
    return true;
  }
  RxStatus Receive(CanFrame& frame, uint32_t timeout_us) noexcept {
    static_cast<void>(frame);
    static_cast<void>(timeout_us);
    now_us += 10;
    return RxStatus::Timeout;
  }
  [[nodiscard]] uint32_t NowUs() const noexcept {
    return now_us;
  }
};

} // namespace

TEST_CASE("A port that returns early does not shorten the timeout") {
  EarlyPort port;
  canopen_client::Config c{};
  c.sdo_timeout_us = 200'000;
  c.abort_on_timeout = false;
  canopen_client::Client<EarlyPort> client(port, c);
  REQUIRE(client.EnsureInitialized().ok());
  CHECK(client.SdoReadU8(5, 0x2002, 0).error == Error::Timeout);
  CHECK(port.now_us >= 200'000U); // not 256 reads of 10 us
  CHECK_EQ(client.GetStats().flood_aborts, 0U);
}

TEST_CASE("A flood of frames is cut off and counted") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.responds = false;
  for (uint32_t i = 0; i < canopen_client::kMaxFramesPerWait + 10U; ++i) {
    r.Bus().Inject(Frame(0x305, {1}), r.Bus().now_us + 1 + i);
  }
  CHECK(r.client.SdoReadU8(5, 0x2002, 0).error == Error::Timeout);
  CHECK_EQ(r.client.GetStats().flood_aborts, 1U);
}

// ---- the port sees a member, not a local -------------------------------------------------------

TEST_CASE("Send is given a frame that lives in the driver object") {
  Rig r;
  REQUIRE(r.started);
  static_cast<void>(r.client.SendNmt(NmtCommand::Start, 5));
  const auto object = std::as_bytes(std::span<const decltype(r.client), 1>(&r.client, 1));
  const auto inside = [&object](const void* p) {
    const std::less<> before;
    return !before(p, object.data()) && before(p, object.subspan(object.size()).data());
  };
  CHECK(inside(r.Bus().last_sent_ptr));
  static_cast<void>(r.client.ReadDeviceType(5));
  CHECK(inside(r.Bus().last_sent_ptr));
}

// ---- reply validity ---------------------------------------------------------------------------

TEST_CASE("A download segment answered with the wrong toggle fails the transfer") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585 && (f.data[0] >> 5) == 1) {
      f.data[0] ^= 0x10U;
    }
  };
  const std::array<uint8_t, 12> data{'C', 'A', 'N', 'o', 'p', 'e', 'n', ' ', 'n', 'o', 'd', 'e'};
  const auto e = r.client.SdoDownload(5, 0x2100, 0, data);
  CHECK(!e.ok() && e.error == Error::Protocol);
  CHECK_EQ(r.n5.last_abort_code, 0x05030000U);
}

TEST_CASE("Strict SDO length covers the initiate and segment download responses") {
  for (int strict = 1; strict >= 0; --strict) {
    canopen_client::Config c = Quick();
    c.strict_sdo_length = strict != 0;
    Rig r(c);
    REQUIRE(r.started);
    r.Bus().mutate = [](CanFrame& f, bool&) {
      if (f.id == 0x585 && f.data[0] == 0x60) {
        f.dlc = 4; // an initiate download response cut to its multiplexer
      }
    };
    const auto w = r.client.SdoWriteU16(5, 0x2001, 0, 7);
    CHECK_EQ(w.ok(), strict == 0);
    if (strict != 0) {
      CHECK(w.error == Error::Protocol);
    }
    r.Bus().mutate = [](CanFrame& f, bool&) {
      if (f.id == 0x585 && (f.data[0] >> 5) == 1) {
        f.dlc = 1; // a segment response cut to its command byte
      }
    };
    const std::array<uint8_t, 12> data{};
    const auto d = r.client.SdoDownload(5, 0x2100, 0, data);
    CHECK_EQ(d.ok(), strict == 0);
  }
}

TEST_CASE("A unicast NMT command leaves the other monitored nodes alone") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 1'000'000).ok());
  REQUIRE(r.client.MonitorNode(6, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 10);
  r.Bus().Inject(Frame(0x706, {0x05}), r.Bus().now_us + 20);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  CHECK(r.client.SendNmt(NmtCommand::Stop, 5).ok());
  CHECK(r.client.GetNodeStatus(5).value.state == NmtState::Stopped);
  CHECK(r.client.GetNodeStatus(6).value.state == NmtState::Operational);
  CHECK(r.client.SdoReadU32(6, 0x1000, 0).ok()); // node 6 is not blocked
  CHECK(r.client.SdoReadU32(5, 0x1000, 0).error == Error::NodeState);
  // Reset of one node forgets only that node.
  CHECK(r.client.SendNmt(NmtCommand::ResetNode, 5).ok());
  CHECK(!r.client.GetNodeStatus(5).value.seen);
  CHECK(r.client.GetNodeStatus(6).value.seen);
}

TEST_CASE("Node 127 and node 1 sit inside every identifier range") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().AddNode(127);
  r.Bus().AddNode(1);
  for (const uint16_t id : {uint16_t{0x0FF}, uint16_t{0x081}}) {
    r.Bus().Inject(Frame(id, {0x10, 0x23, 0x01, 0, 0, 0, 0, 0}), r.Bus().now_us + 10);
    CHECK(r.client.Poll(1'000).value == FrameClass::Emcy);
  }
  canopen_client::EmcyEvent e{};
  REQUIRE(r.client.PopEmcy(e));
  CHECK_EQ(e.node, 127);
  REQUIRE(r.client.PopEmcy(e));
  CHECK_EQ(e.node, 1);
  // 0x080 is SYNC, not an emergency of node 0.
  r.Bus().Inject(Frame(0x080, {0x10, 0x23, 0x01, 0, 0, 0, 0, 0}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Sync);
  // SDO replies nobody waits for, at both ends of the range, are counted as stale.
  const uint32_t before = r.client.GetStats().sdo_stale;
  r.Bus().Inject(Frame(0x581, {0x43, 0, 0x10, 0, 0, 0, 0, 0}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  r.Bus().Inject(Frame(0x5FF, {0x43, 0, 0x10, 0, 0, 0, 0, 0}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  CHECK_EQ(r.client.GetStats().sdo_stale, before + 2U);
  // Heartbeats at both ends.
  REQUIRE(r.client.MonitorNode(127, 1'000'000).ok());
  REQUIRE(r.client.MonitorNode(1, 1'000'000).ok());
  r.Bus().Inject(Frame(0x77F, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  r.Bus().Inject(Frame(0x701, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  // An SDO exchange with node 127 works end to end.
  r.Bus().Node(127)->Add(0x2010, 0, {1, 0, 0, 0}, true, true);
  CHECK(r.client.SdoReadU32(127, 0x2010, 0).ok());
}

TEST_CASE("A 4-byte download is expedited and a 5-byte one is segmented") {
  Rig r;
  REQUIRE(r.started);
  std::size_t mark = r.Sent();
  const std::array<uint8_t, 4> four{1, 2, 3, 4};
  CHECK(r.client.SdoDownload(5, 0x2000, 0, four).ok());
  CHECK_EQ(Count(r, mark, 0x605), 1U);
  CHECK_EQ(r.Bus().sent[mark].data[0], 0x23);
  mark = r.Sent();
  const std::array<uint8_t, 5> five{1, 2, 3, 4, 5};
  CHECK(!r.client.SdoDownload(5, 0x2000, 0, five).ok()); // the 32-bit object refuses 5 bytes
  CHECK_EQ(r.Bus().sent[mark].data[0], 0x21);            // but it was offered as a segmented one
}

TEST_CASE("An upload that exactly fills the buffer is not too big") {
  Rig r;
  REQUIRE(r.started);
  std::array<uint8_t, 12> exact{};
  const auto seg = r.client.SdoUpload(5, 0x2100, 0, exact); // 12 bytes, segmented
  REQUIRE(seg.ok());
  CHECK_EQ(seg.value, 12U);
  std::array<uint8_t, 4> four{};
  const auto exp = r.client.SdoUpload(5, 0x2000, 0, four); // 4 bytes, expedited
  REQUIRE(exp.ok());
  CHECK_EQ(exp.value, 4U);
  std::array<uint8_t, 11> short_by_one{};
  CHECK(r.client.SdoUpload(5, 0x2100, 0, short_by_one).error == Error::BufferTooSmall);
  std::array<uint8_t, 3> three{};
  CHECK(r.client.SdoUpload(5, 0x2000, 0, three).error == Error::BufferTooSmall);
}

TEST_CASE("A guard request to a node nobody watches is not sent") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  CHECK(r.client.GuardNode(9).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
}

TEST_CASE("A guard reply with the wrong toggle fails once and the exchange resumes") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 1'000'000).ok());
  r.n5.guard_toggle =
      true; // the node's first reply carries the bit set; the client expects it clear
  CHECK(r.client.GuardNode(5).error == Error::Protocol);
  CHECK(r.client.GuardNode(5).ok());
  CHECK(r.client.GuardNode(5).ok());
}

TEST_CASE("TPDO subscription limits are inclusive") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.SubscribeTpdo(0x181, 1).ok());
  CHECK(r.client.SubscribeTpdo(0x57F, 8).ok());
  CHECK(r.client.SubscribeTpdo(0x180, 4).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x580, 4).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x185, 0).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x185, 9).error == Error::InvalidArgument);
}

TEST_CASE("PDO bit fields may be 64 bits wide and may end on the last bit") {
  std::array<uint8_t, 8> d{};
  for (std::size_t i = 0; i < d.size(); ++i) {
    d[i] = static_cast<uint8_t>(0x11 * (i + 1));
  }
  const auto all = canopen_client::ExtractBits(d, 0, 64);
  REQUIRE(all.ok());
  CHECK(all.value == 0x8877665544332211ULL);
  const auto last = canopen_client::ExtractBits(d, 56, 8);
  REQUIRE(last.ok());
  CHECK_EQ(last.value, 0x88U);
  CHECK(!canopen_client::ExtractBits(d, 57, 8).ok());
  CHECK(!canopen_client::ExtractBits(d, 0, 65).ok());
  CHECK(canopen_client::InsertBits(d, 0, 64, 0).ok());
  CHECK(canopen_client::InsertBits(d, 56, 8, 0xFF).ok());
  CHECK_EQ(d[7], 0xFF);
  CHECK(!canopen_client::InsertBits(d, 57, 8, 0).ok());
  CHECK(!canopen_client::InsertBits(d, 0, 65, 0).ok());
  CHECK(!canopen_client::InsertBits(d, 0, 0, 0).ok());
}

// ---- validation, codes, flags -------------------------------------------------------------------

TEST_CASE("Reserved PDO transmission types are refused, the others pass") {
  PdoConfig c{};
  c.cob_id = 0x185;
  c.entry_count = 1;
  c.entries[0] = {0x2002, 0, 8};
  for (unsigned t : {0U, 1U, 240U, 252U, 253U, 254U, 255U}) {
    c.transmission_type = static_cast<uint8_t>(t);
    CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  }
  for (unsigned t : {241U, 246U, 251U}) {
    c.transmission_type = static_cast<uint8_t>(t);
    CHECK(canopen_client::ValidatePdoConfig(c) == Error::InvalidArgument);
  }
  Rig r;
  REQUIRE(r.started);
  c.transmission_type = 245;
  const std::size_t mark = r.Sent();
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, c).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
}

TEST_CASE("The abort code for a missing SDO connection resource has a name") {
  CHECK(std::string_view(canopen_client::AbortCodeName(0x060A0023U)) ==
        std::string_view("SdoResourceUnavailable"));
  CHECK(std::string_view(canopen_client::AbortCodeName(0x06020000U)) ==
        std::string_view("ObjectNotFound"));
  CHECK(std::string_view(canopen_client::AbortCodeName(0x12345678U)) ==
        std::string_view("Unknown"));
}

TEST_CASE("ClearBootUp forgets a seen boot-up message of a watched node only") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x00}), r.Bus().now_us + 10);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  CHECK(r.client.GetNodeStatus(5).value.bootup_seen);
  CHECK(r.client.ClearBootUp(5).ok());
  CHECK(!r.client.GetNodeStatus(5).value.bootup_seen);
  CHECK(r.client.ClearBootUp(9).error == Error::InvalidArgument);
  canopen_client::Client<MockPort> cold(r.port);
  CHECK(cold.ClearBootUp(5).error == Error::NotInitialized);
}

// ---- round 2: the drive time bound, raw reads, guard boot-up, NMT reset, Stopped gate ---------

namespace {

/// A slow node: every reply takes 100 ms, the drive wait is 1 s, the poll interval 1 ms.
canopen_client::Config SlowReplies() {
  canopen_client::Config c{};
  c.drive_poll_interval_us = 1'000;
  c.drive_timeout_us = 1'000'000;
  c.sdo_timeout_us = 500'000;
  c.abort_on_timeout = false;
  return c;
}

constexpr uint64_t kNever = 1'000'000'000'000ULL; // a drive transition that never shows

} // namespace

TEST_CASE("DriveEnable on a slow node gives up near drive_timeout_us, not after a step count") {
  Rig r(SlowReplies());
  REQUIRE(r.started);
  r.Bus().reply_latency_us = 100'000;
  r.n5.drive.transition_delay_us = kNever;
  const uint64_t t0 = r.Bus().now_us;
  CHECK(r.client.DriveEnable(5).error == Error::Timeout);
  CHECK(r.Bus().now_us - t0 >= 1'000'000U);
  CHECK(r.Bus().now_us - t0 < 1'300'000U);
}

TEST_CASE("DriveResetFault on a slow node ends near drive_timeout_us in both of its waits") {
  {
    // The fault reaction never ends: the first wait times out.
    Rig r(SlowReplies());
    REQUIRE(r.started);
    r.Bus().reply_latency_us = 100'000;
    r.n5.drive.fault_reaction_us = kNever;
    r.n5.drive.RaiseFault(r.Bus().now_us);
    const uint64_t t0 = r.Bus().now_us;
    CHECK(r.client.DriveResetFault(5).error == Error::Timeout);
    CHECK(r.Bus().now_us - t0 >= 1'000'000U);
    CHECK(r.Bus().now_us - t0 < 1'300'000U);
  }
  {
    // The fault stays after the reset: the wait for it to clear times out.
    Rig r(SlowReplies());
    REQUIRE(r.started);
    r.Bus().reply_latency_us = 100'000;
    r.n5.drive.fault_persists = true;
    Put(r.n5, SimDrive::State::Fault);
    const uint64_t t0 = r.Bus().now_us;
    CHECK(r.client.DriveResetFault(5).error == Error::DriveFault);
    CHECK(r.Bus().now_us - t0 >= 1'000'000U);
    CHECK(r.Bus().now_us - t0 < 1'500'000U);
  }
}

TEST_CASE("DriveResetFault spends one drive_timeout_us in all, not one per wait") {
  Rig r(SlowReplies());
  REQUIRE(r.started);
  r.Bus().reply_latency_us = 1'000;
  r.n5.drive.fault_reaction_us = 600'000; // the first wait takes 0.6 s
  r.n5.drive.fault_persists = true;       // the second would take another full second
  r.n5.drive.RaiseFault(r.Bus().now_us);
  const uint64_t t0 = r.Bus().now_us;
  CHECK(r.client.DriveResetFault(5).error == Error::DriveFault);
  CHECK(r.Bus().now_us - t0 >= 1'000'000U);
  CHECK(r.Bus().now_us - t0 < 1'200'000U);
}

TEST_CASE("DriveResetFault leaves the controlword at 0, bit 7 low, once the fault is gone") {
  Rig r;
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Fault);
  CHECK(r.client.DriveResetFault(5).ok());
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
  CHECK_EQ(r.n5.drive.controlword, 0x0000);
}

TEST_CASE("SetDriveMode read-back on a slow node gives up near drive_timeout_us") {
  Rig r(SlowReplies());
  REQUIRE(r.started);
  r.Bus().reply_latency_us = 100'000;
  r.n5.drive.mode_latency_us = kNever;
  const uint64_t t0 = r.Bus().now_us;
  const auto e = r.client.SetDriveMode(5, canopen_client::OperationMode::ProfileVelocity, {},
                                       canopen_client::Verify::ReadBack);
  CHECK(e.error == Error::DeviceFault);
  CHECK(r.Bus().now_us - t0 >= 1'000'000U);
  CHECK(r.Bus().now_us - t0 < 1'400'000U);
}

TEST_CASE("The drive wait ends at the timeout itself: elapsed equal to it is over") {
  canopen_client::Config c = SlowReplies();
  c.drive_timeout_us = 100'000; // one reply latency
  Rig r(c);
  REQUIRE(r.started);
  r.Bus().reply_latency_us = 100'000;
  r.n5.drive.mode_latency_us = kNever;
  const uint64_t t0 = r.Bus().now_us;
  const auto e = r.client.SetDriveMode(5, canopen_client::OperationMode::ProfileVelocity, {},
                                       canopen_client::Verify::ReadBack);
  CHECK(e.error == Error::DeviceFault);
  // The write (100 ms) and one read (100 ms) in all: a second read would make it 300 ms.
  CHECK(r.Bus().now_us - t0 < 250'000U);
}

TEST_CASE("ReadRawSdo drains first: a stale queued reply is not returned for the new request") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().Inject(Frame(0x585, {0x43, 0x00, 0x10, 0x00, 1, 2, 3, 4}), r.Bus().now_us);
  const auto reply = r.client.ReadRawSdo(5, 0x2000, 0);
  REQUIRE(reply.ok());
  CHECK_EQ(reply.value.data[1], 0x00);
  CHECK_EQ(reply.value.data[2], 0x20); // the reply to 0x2000, not the queued one to 0x1000
}

TEST_CASE("A boot-up frame during a guard request is not the guard reply") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 1'000'000).ok());
  r.n5.guarding = false;
  // The node was reset while the request was out: boot-up (toggle 0), then its first reply.
  r.Bus().Inject(Frame(0x705, {0x00}), r.Bus().now_us + 100);
  r.Bus().Inject(Frame(0x705, {0x7F}), r.Bus().now_us + 300);
  const auto g = r.client.GuardNode(5);
  REQUIRE(g.ok());
  CHECK(g.value == NmtState::PreOperational);
  CHECK(r.client.GetNodeStatus(5).value.bootup_seen);
  // The toggle moved once, for the real reply: the node's next reply (bit 7 set) fits.
  r.n5.guarding = true;
  r.n5.guard_toggle = true;
  CHECK(r.client.GuardNode(5).ok());
}

TEST_CASE("An NMT reset the driver sends clears the boot-up flag at once") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 100'000).ok());
  REQUIRE(r.client.SendNmt(NmtCommand::ResetNode, 5).ok());
  REQUIRE(r.client.Poll(10'000).value == FrameClass::Heartbeat);
  REQUIRE(r.client.GetNodeStatus(5).value.bootup_seen);
  REQUIRE(r.client.SendNmt(NmtCommand::ResetCommunication, 5).ok());
  CHECK(!r.client.GetNodeStatus(5).value.bootup_seen); // before the new boot-up arrives
  CHECK(r.client.SendNmt(NmtCommand::Start, 5).ok());
}

TEST_CASE("The Stopped gate lapses with the heartbeat's age, and holds up to the limit") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(6, 100'000).ok());
  r.Bus().Inject(Frame(0x706, {0x04}), r.Bus().now_us + 10);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  CHECK(r.client.SdoReadU32(6, 0x1000, 0).error == Error::NodeState);
  const uint64_t heard = r.Bus().now_us;
  CHECK(r.client.Poll(100'000).value == FrameClass::None);
  CHECK_EQ(r.Bus().now_us - heard, 100'000U);
  // Exactly the node life time since the heartbeat: still Stopped, still refused.
  CHECK(r.client.SdoReadU32(6, 0x1000, 0).error == Error::NodeState);
  CHECK(r.client.Poll(50'000).value == FrameClass::None);
  // Long past it: the old Stopped state says nothing now, the node is asked.
  CHECK(r.client.SdoReadU32(6, 0x1000, 0).ok());
}

TEST_CASE("A wait is cut off at exactly the frame budget, not one frame later") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.responds = false;
  for (uint32_t i = 0; i < canopen_client::kMaxFramesPerWait; ++i) {
    r.Bus().Inject(Frame(0x305, {1}), r.Bus().now_us + 1 + i);
  }
  CHECK(r.client.SdoReadU8(5, 0x2002, 0).error == Error::Timeout);
  CHECK_EQ(r.client.GetStats().flood_aborts, 1U);
  // One frame fewer: the wait runs to its own timeout instead, no flood abort.
  Rig q(Quick());
  REQUIRE(q.started);
  q.n5.responds = false;
  for (uint32_t i = 0; i + 1U < canopen_client::kMaxFramesPerWait; ++i) {
    q.Bus().Inject(Frame(0x305, {1}), q.Bus().now_us + 1 + i);
  }
  CHECK(q.client.SdoReadU8(5, 0x2002, 0).error == Error::Timeout);
  CHECK_EQ(q.client.GetStats().flood_aborts, 0U);
}

TEST_CASE("A drive pause under a flood ends at the frame budget, and a Timeout comes sooner") {
  // The pause between two polls reads frames until the budget is spent; the wait then goes on
  // with the next poll, so under a flood the step limit (not drive_timeout_us) can end it first,
  // always on the early side. 256 frames in the first pause: it ends after them, not after 50 ms.
  canopen_client::Config c{};
  c.drive_poll_interval_us = 50'000;
  c.drive_timeout_us = 100'000;
  c.sdo_timeout_us = 20'000;
  c.abort_on_timeout = false;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.drive.mode_latency_us = kNever;
  const std::size_t mark = r.Sent();
  for (uint32_t i = 0; i < canopen_client::kMaxFramesPerWait; ++i) {
    r.Bus().Inject(Frame(0x305, {1}), r.Bus().now_us + 5'000 + i);
  }
  CHECK(r.client
            .SetDriveMode(5, canopen_client::OperationMode::ProfileVelocity, {},
                          canopen_client::Verify::ReadBack)
            .error == Error::DeviceFault);
  // The write and four reads: the flooded pause was short, so more reads fit before the timeout
  // than the two a full 50 ms pause would leave.
  CHECK_EQ(Count(r, mark, 0x605), 5U);
}

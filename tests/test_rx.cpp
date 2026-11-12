/**
 * @file test_rx.cpp
 * @brief The receive path: NMT, heartbeat, node guarding, EMCY, SYNC and TPDO, against the
 * simulated bus.
 */
#include <array>
#include <cstdint>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using canopen_client::FrameClass;
using canopen_client::NmtCommand;
using canopen_client::NmtState;
using namespace canopen_client_test;

TEST_CASE("NMT commands reach the node and change its state") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.SendNmt(NmtCommand::Start, 5).ok());
  CHECK_EQ(r.n5.nmt, 5);
  CHECK_EQ(r.n6.nmt, 0x7F);
  CHECK(r.client.SendNmt(NmtCommand::Stop, 5).ok());
  CHECK_EQ(r.n5.nmt, 4);
  CHECK(r.client.SendNmt(NmtCommand::Start, 0).ok()); // broadcast
  CHECK_EQ(r.n5.nmt, 5);
  CHECK_EQ(r.n6.nmt, 5);
  CHECK(r.client.SendNmt(NmtCommand::PreOperational, 6).ok());
  CHECK_EQ(r.n6.nmt, 0x7F);
  const CanFrame& last = r.Bus().sent.back();
  CHECK(last.id == 0x000 && last.dlc == 2 && last.data[0] == 0x80 && last.data[1] == 6);
}

TEST_CASE("A reset command produces a boot-up message the monitor sees") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  CHECK(r.client.SendNmt(NmtCommand::ResetNode, 5).ok());
  const auto c = r.client.Poll(10'000);
  CHECK(c.ok() && c.value == FrameClass::Heartbeat);
  const auto s = r.client.GetNodeStatus(5);
  CHECK(s.ok() && s.value.seen && s.value.bootup_seen && s.value.state == NmtState::BootUp);
  CHECK_EQ(s.value.count, 1U);
}

TEST_CASE("An NMT command with a bad node or command sends nothing") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  CHECK(r.client.SendNmt(NmtCommand::Start, 128).error == Error::InvalidArgument);
  CHECK(r.client.SendNmt(ut::RawEnum<NmtCommand>(0x03), 5).error == Error::InvalidArgument);
  CHECK(r.client.SendNmt(ut::RawEnum<NmtCommand>(0x00), 5).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
  r.Bus().send_ok = false;
  CHECK(r.client.SendNmt(NmtCommand::Start, 5).error == Error::Bus);
  CHECK_EQ(r.n5.nmt, 0x7F);
}

TEST_CASE("A failed NMT send leaves the node table as it was") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).ok());
  r.Bus().send_ok = false;
  CHECK(!r.client.SendNmt(NmtCommand::Stop, 5).ok());
  CHECK(r.client.GetNodeStatus(5).value.state == NmtState::Operational);
}

TEST_CASE("Heartbeats update the node table and age out") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 50'000).ok());
  const auto before = r.client.GetNodeStatus(5);
  CHECK(before.ok() && !before.value.seen && !before.value.alive);
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 100);
  const auto c = r.client.Poll(1'000);
  CHECK(c.ok() && c.value == FrameClass::Heartbeat);
  auto s = r.client.GetNodeStatus(5);
  CHECK(s.ok() && s.value.seen && s.value.alive && s.value.state == NmtState::Operational);
  CHECK(!s.value.bootup_seen);
  CHECK(r.client.Poll(40'000).value == FrameClass::None);
  CHECK(r.client.GetNodeStatus(5).value.alive);
  CHECK(r.client.Poll(20'000).value == FrameClass::None); // 60 ms since the heartbeat
  s = r.client.GetNodeStatus(5);
  CHECK(s.ok() && s.value.seen && !s.value.alive);
}

TEST_CASE("A heartbeat from a node nobody watches, or with a toggle bit, is ignored") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 50'000).ok());
  r.Bus().Inject(Frame(0x706, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  r.Bus().Inject(Frame(0x705, {0x85}), r.Bus().now_us + 10); // toggle bit set: a guarding reply
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  CHECK(!r.client.GetNodeStatus(5).value.seen);
}

TEST_CASE("Invalid heartbeat and EMCY frames are counted and dropped") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 50'000).ok());
  r.Bus().Inject(Frame(0x705, {0x05, 0x00}), r.Bus().now_us + 10); // two bytes
  CHECK(r.client.Poll(1'000).value == FrameClass::Invalid);
  r.Bus().Inject(Frame(0x705, {0x33}), r.Bus().now_us + 10); // not a state
  CHECK(r.client.Poll(1'000).value == FrameClass::Invalid);
  r.Bus().Inject(Frame(0x085, {1, 2, 3, 4, 5, 6, 7}), r.Bus().now_us + 10); // 7 bytes
  CHECK(r.client.Poll(1'000).value == FrameClass::Invalid);
  CHECK_EQ(r.client.GetStats().frames_invalid, 3U);
  CHECK(!r.client.GetNodeStatus(5).value.seen);
  canopen_client::EmcyEvent none{};
  CHECK(!r.client.PopEmcy(none));
}

TEST_CASE("The monitor table has a fixed size, re-registering keeps what was received") {
  Rig r;
  REQUIRE(r.started);
  for (uint8_t n = 1; n <= canopen_client::kMonitorCapacity; ++n) {
    CHECK(r.client.MonitorNode(n, 10'000).ok());
  }
  CHECK(r.client.MonitorNode(100, 10'000).error == Error::BufferTooSmall);
  CHECK(r.client.MonitorNode(3, 20'000).ok()); // an existing node: no slot needed
  CHECK(r.client.UnmonitorNode(3).ok());
  CHECK(r.client.MonitorNode(100, 10'000).ok());
  CHECK(r.client.UnmonitorNode(3).error == Error::InvalidArgument); // no longer watched
  CHECK(r.client.GetNodeStatus(3).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(0, 10'000).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(128, 10'000).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(5, 999).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(5, 60'000'001).error == Error::InvalidArgument);
  // Re-registering node 5 with a new timeout keeps its count.
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  CHECK(r.client.MonitorNode(5, 500'000).ok());
  CHECK_EQ(r.client.GetNodeStatus(5).value.count, 1U);
}

TEST_CASE("A node's own heartbeat producer is seen at its period") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 30'000).ok());
  CHECK(r.client.WriteHeartbeatProducerTime(5, 10).ok());
  uint32_t beats = 0;
  for (int i = 0; i < 20; ++i) {
    const auto c = r.client.Poll(5'000);
    if (c.ok() && c.value == FrameClass::Heartbeat) {
      ++beats;
    }
  }
  CHECK(beats >= 8 && beats <= 11);
  CHECK(r.client.GetNodeStatus(5).value.alive);
  CHECK(r.client.WriteHeartbeatProducerTime(5, 0).ok()); // the node stops
  r.Bus().rx.clear();
  CHECK(r.client.Poll(100'000).value == FrameClass::None);
  CHECK(!r.client.GetNodeStatus(5).value.alive);
}

TEST_CASE("Node guarding: the toggle bit alternates, a wrong one resynchronises") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  const auto a = r.client.GuardNode(5);
  CHECK(a.ok() && a.value == NmtState::PreOperational);
  const CanFrame& req = r.Bus().sent.back();
  CHECK(req.rtr && req.id == 0x705 && req.dlc == 0);
  CHECK(r.client.GuardNode(5).ok());
  CHECK(r.client.GuardNode(5).ok());
  // The node's toggle slips (it was reset without the client knowing).
  r.n5.guard_toggle = !r.n5.guard_toggle;
  const auto bad = r.client.GuardNode(5);
  CHECK(!bad.ok() && bad.error == Error::Protocol);
  // After the failure the client expects the bit after the one it saw, so the node's next reply
  // fits.
  CHECK(r.client.GuardNode(5).ok());
  CHECK(r.client.GetNodeStatus(5).value.count >= 4U);
}

TEST_CASE("Node guarding after a reset starts with the toggle bit clear") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  CHECK(r.client.GuardNode(5).ok());
  CHECK(r.client.GuardNode(5).ok()); // the client now expects 0 again after 0, 1
  CHECK(r.client.GuardNode(5).ok());
  CHECK(r.client.SendNmt(NmtCommand::ResetCommunication, 5).ok());
  r.Bus().rx.clear();
  const auto a = r.client.GuardNode(5);
  CHECK(a.ok());
}

TEST_CASE("Node guarding reports a silent node, an unwatched node and a bus error") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  r.n5.guarding = false;
  const uint64_t t0 = r.Bus().now_us;
  const auto t = r.client.GuardNode(5);
  CHECK(!t.ok() && t.error == Error::Timeout);
  CHECK(r.Bus().now_us - t0 >= 100'000U);
  CHECK(r.client.GuardNode(6).error == Error::InvalidArgument); // not monitored
  CHECK(r.client.GuardNode(0).error == Error::InvalidArgument);
  const std::size_t mark = r.Sent();
  CHECK(r.client.GuardNode(200).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
  r.n5.guarding = true;
  r.Bus().send_ok = false;
  CHECK(r.client.GuardNode(5).error == Error::Bus);
  r.Bus().send_ok = true;
  r.Bus().receive_error = true;
  CHECK(r.client.GuardNode(5).error == Error::Bus);
}

TEST_CASE("EMCY frames are queued in order and decoded") {
  Rig r;
  REQUIRE(r.started);
  canopen_client::EmcyEvent e{};
  CHECK(!r.client.PopEmcy(e));
  r.Bus().Emcy(5, 0x8130, 0x11, {1, 2, 3, 4, 5});
  const auto c = r.client.Poll(5'000);
  CHECK(c.ok() && c.value == FrameClass::Emcy);
  r.Bus().Emcy(6, 0x0000, 0x00);
  CHECK(r.client.Poll(5'000).value == FrameClass::Emcy);
  REQUIRE(r.client.PopEmcy(e));
  CHECK(e.node == 5 && e.code == 0x8130 && e.error_register == 0x11);
  CHECK(e.manufacturer[0] == 1 && e.manufacturer[4] == 5 && e.timestamp_us != 0);
  REQUIRE(r.client.PopEmcy(e));
  CHECK(e.node == 6 && e.code == 0x0000); // error reset
  CHECK(!r.client.PopEmcy(e));
}

TEST_CASE("A full EMCY queue drops the newest and counts it") {
  Rig r;
  REQUIRE(r.started);
  for (uint16_t i = 0; i < canopen_client::kEmcyQueueCapacity + 2; ++i) {
    r.Bus().Emcy(5, static_cast<uint16_t>(0x1000 + i), 0x01);
    CHECK(r.client.Poll(5'000).value == FrameClass::Emcy);
  }
  CHECK_EQ(r.client.GetStats().emcy_dropped, 2U);
  canopen_client::EmcyEvent e{};
  for (uint16_t i = 0; i < canopen_client::kEmcyQueueCapacity; ++i) {
    REQUIRE(r.client.PopEmcy(e));
    CHECK_EQ(e.code, static_cast<uint16_t>(0x1000U + i));
  }
  CHECK(!r.client.PopEmcy(e));
}

TEST_CASE("An EMCY that arrives during an SDO exchange is queued, not lost") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().Inject(Frame(0x085, {0x30, 0x81, 0x01, 0, 0, 0, 0, 0}), r.Bus().now_us + 50);
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).ok());
  canopen_client::EmcyEvent e{};
  CHECK(r.client.PopEmcy(e));
  CHECK(e.node == 5 && e.code == 0x8130);
}

TEST_CASE("A foreign SYNC is classified, SYNC frames carry the optional counter") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().Inject(Frame(0x080, {}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Sync);
  CHECK(r.client.SendSync().ok());
  CHECK(r.client.SendSync(3).ok());
  CHECK(r.Bus().sent[r.Sent() - 2].dlc == 0);
  const CanFrame& s = r.Bus().sent.back();
  CHECK(s.id == 0x080 && s.dlc == 1 && s.data[0] == 3);
  CHECK(r.client.SendSync(241).error == Error::InvalidArgument);
  CHECK_EQ(r.Bus().sent.size(), 2U);
}

TEST_CASE("The SYNC producer keeps its period, never bursts, and counts 1 to the overflow") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.ServiceSync().ok() && !r.client.ServiceSync().value); // stopped: nothing
  CHECK(r.client.ConfigureSync(10'000, 3).ok());
  CHECK(r.client.ServiceSync().value); // the first call sends at once
  CHECK(!r.client.ServiceSync().value);
  std::array<uint8_t, 5> seen{};
  seen[0] = r.Bus().sent.back().data[0];
  for (std::size_t i = 1; i < seen.size(); ++i) {
    CHECK(r.client.Poll(10'000).ok());
    const auto due = r.client.ServiceSync();
    CHECK(due.ok() && due.value);
    seen[i] = r.Bus().sent.back().data[0];
  }
  CHECK(seen[0] == 1 && seen[1] == 2 && seen[2] == 3 && seen[3] == 1 && seen[4] == 2);
  // A late call sends one SYNC, not a burst.
  const std::size_t mark = r.Sent();
  CHECK(r.client.Poll(100'000).ok());
  CHECK(r.client.ServiceSync().value);
  CHECK(!r.client.ServiceSync().value);
  CHECK_EQ(r.Sent(), mark + 1);
  // Stopping, and a period without a counter.
  CHECK(r.client.ConfigureSync(0, 0).ok());
  CHECK(!r.client.ServiceSync().value);
  CHECK(r.client.ConfigureSync(5'000, 0).ok());
  CHECK(r.client.ServiceSync().value);
  CHECK_EQ(r.Bus().sent.back().dlc, 0);
}

TEST_CASE("SYNC configuration is range checked and a failed send is reported") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.ConfigureSync(99, 0).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(60'000'001, 0).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(1'000, 1).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(1'000, 241).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(100, 240).ok());
  CHECK(r.client.ConfigureSync(1'000, 2).ok());
  r.Bus().send_ok = false;
  CHECK(r.client.ServiceSync().error == Error::Bus);
  r.Bus().send_ok = true;
  CHECK(r.client.ServiceSync().value); // the failed one did not start the period
}

TEST_CASE("Subscribed TPDOs hold the latest frame, others are ignored") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.SubscribeTpdo(0x185, 3).ok());
  CHECK(r.client.ReadTpdo(0x185).value.sequence == 0);
  r.Bus().Inject(Frame(0x185, {1, 2, 3}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Pdo);
  r.Bus().Inject(Frame(0x185, {4, 5, 6}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Pdo);
  const auto s = r.client.ReadTpdo(0x185);
  REQUIRE(s.ok());
  CHECK(s.value.sequence == 2 && s.value.dlc == 3 && s.value.cob_id == 0x185);
  CHECK(s.value.data[0] == 4 && s.value.data[2] == 6 && s.value.timestamp_us != 0);
  r.Bus().Inject(Frame(0x186, {1, 2, 3}), r.Bus().now_us + 10); // not subscribed
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  r.Bus().Inject(Frame(0x185, {1, 2}), r.Bus().now_us + 10); // wrong length
  CHECK(r.client.Poll(1'000).value == FrameClass::Invalid);
  CHECK(r.client.ReadTpdo(0x185).value.sequence == 2);
}

TEST_CASE("TPDO subscriptions: capacity, arguments, resubscribe and unsubscribe") {
  Rig r;
  REQUIRE(r.started);
  for (uint16_t i = 0; i < canopen_client::kPdoSubscriptionCapacity; ++i) {
    CHECK(r.client.SubscribeTpdo(static_cast<uint16_t>(0x181 + i), 8).ok());
  }
  CHECK(r.client.SubscribeTpdo(0x300, 8).error == Error::BufferTooSmall);
  CHECK(r.client.SubscribeTpdo(0x181, 2).ok()); // changes the length, needs no slot
  CHECK(r.client.SubscribeTpdo(0x180, 2).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x580, 2).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x185, 0).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x185, 9).error == Error::InvalidArgument);
  CHECK(r.client.UnsubscribeTpdo(0x181).ok());
  CHECK(r.client.UnsubscribeTpdo(0x181).error == Error::InvalidArgument);
  CHECK(r.client.ReadTpdo(0x181).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(0x300, 8).ok());
}

TEST_CASE("Poll: nothing, a bus error, invalid and remote frames, stale SDO replies") {
  Rig r;
  REQUIRE(r.started);
  const uint64_t t0 = r.Bus().now_us;
  const auto none = r.client.Poll(7'000);
  CHECK(none.ok() && none.value == FrameClass::None);
  CHECK(r.Bus().now_us - t0 == 7'000U);
  CHECK(r.client.Poll(0).value == FrameClass::None);
  CanFrame bad{};
  bad.id = 0x800; // not an 11-bit identifier
  r.Bus().Inject(bad, r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Invalid);
  CanFrame longf{};
  longf.id = 0x185;
  longf.dlc = 9;
  r.Bus().Inject(longf, r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Invalid);
  r.Bus().Inject(Frame(0x705, {}, true), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  r.Bus().Inject(Frame(0x585, {0x43, 0, 0x20, 0, 1, 2, 3, 4}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  CHECK_EQ(r.client.GetStats().sdo_stale, 1U);
  CHECK_EQ(r.client.GetStats().frames_received, 4U);
  CHECK_EQ(r.client.GetStats().frames_invalid, 2U);
  r.Bus().receive_error = true;
  CHECK(r.client.Poll(1'000).error == Error::Bus);
}

/**
 * @file test_faults.cpp
 * @brief A failure at every send and every receive of a multi-step call is reported and leaves a
 * safe state; lying nodes (sizes, segments, replies that are no reply) never write past a buffer.
 */
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using canopen_client::NmtCommand;
using canopen_client::PdoConfig;
using canopen_client::PdoKind;
using namespace canopen_client_test;

namespace {

canopen_client::Config Quick() {
  canopen_client::Config c{};
  c.drive_poll_interval_us = 1'000;
  c.drive_timeout_us = 200'000;
  c.sdo_timeout_us = 20'000;
  c.guard_timeout_us = 20'000;
  c.abort_on_timeout = false;
  return c;
}

PdoConfig Tpdo() {
  PdoConfig c{};
  c.cob_id = 0x185;
  c.transmission_type = 1;
  c.inhibit_time_100us = 5;
  c.event_timer_ms = 100;
  c.entry_count = 2;
  c.entries[0] = {0x2001, 0, 16};
  c.entries[1] = {0x2002, 0, 8};
  return c;
}

using Op = std::function<canopen_client::Error(Rig&)>;

/// Run `op` on a fresh rig with the `k`-th send (or receive) failing, for k = 0, 1, ... until a run
/// succeeds. Every failing run must report `Bus`. Returns the number of steps the call has.
int Sweep(const Op& op, bool sends, const std::function<void(Rig&)>& prepare = {}) {
  for (int k = 0; k < 400; ++k) {
    Rig r(Quick());
    if (!r.started) {
      return -1;
    }
    if (prepare) {
      prepare(r);
    }
    if (sends) {
      r.Bus().fail_send_index = r.Bus().sends + k;
    } else {
      r.Bus().fail_receive_index = r.Bus().receives + k;
    }
    const Error e = op(r);
    if (e == Error::Ok) {
      return k;
    }
    CHECK(e == Error::Bus);
    if (e != Error::Bus) {
      return -1;
    }
  }
  return -1;
}

void Put(SimNode& n, SimDrive::State s) {
  n.drive.state = s;
  n.drive.pending = s;
}

void SweepBoth(const char* name, const Op& op, const std::function<void(Rig&)>& prepare = {}) {
  const int sends = Sweep(op, true, prepare);
  const int receives = Sweep(op, false, prepare);
  if (sends <= 0 || receives <= 0) {
    std::printf("    sweep %s: %d sends, %d receives\n", name, sends, receives);
  }
  CHECK(sends > 0);
  CHECK(receives > 0);
}

} // namespace

TEST_CASE("A failed send or receive at any step of a multi-step call is reported as a bus error") {
  SweepBoth("SdoUpload segmented", [](Rig& r) {
    std::array<uint8_t, 32> b{};
    return r.client.SdoUpload(5, 0x2100, 0, b).error;
  });
  SweepBoth("SdoDownload segmented", [](Rig& r) {
    const std::array<uint8_t, 20> d{};
    return r.client.SdoDownload(5, 0x2100, 0, d).error;
  });
  SweepBoth("GuardNode", [](Rig& r) {
    static_cast<void>(r.client.MonitorNode(5, 100'000));
    return r.client.GuardNode(5).error;
  });
  SweepBoth("ConfigurePdo",
            [](Rig& r) { return r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo()).error; });
  SweepBoth(
      "ReadPdoConfig", [](Rig& r) { return r.client.ReadPdoConfig(5, PdoKind::Transmit, 1).error; },
      [](Rig& r) { static_cast<void>(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo())); });
  SweepBoth("DisablePdo", [](Rig& r) { return r.client.DisablePdo(5, PdoKind::Receive, 1).error; });
  SweepBoth("StoreParameters", [](Rig& r) { return r.client.StoreParameters(5, 1).error; });
  SweepBoth("DriveEnable", [](Rig& r) { return r.client.DriveEnable(5).error; });
  SweepBoth(
      "DriveResetFault", [](Rig& r) { return r.client.DriveResetFault(5).error; },
      [](Rig& r) { Put(r.n5, SimDrive::State::Fault); });
  SweepBoth(
      "DriveResetFault reaction", [](Rig& r) { return r.client.DriveResetFault(5).error; },
      [](Rig& r) { r.n5.drive.RaiseFault(r.Bus().now_us); });
  SweepBoth(
      "SetDriveMode verify",
      [](Rig& r) {
        return r.client
            .SetDriveMode(5, canopen_client::OperationMode::Homing, {},
                          canopen_client::Verify::ReadBack)
            .error;
      },
      [](Rig& r) { r.n5.drive.mode_latency_us = 3'000; });
  SweepBoth("ReadDriveMode", [](Rig& r) { return r.client.ReadDriveMode(5).error; });
  // SYNC only sends: a failed send is reported, and there is no receive to fail.
  CHECK(Sweep([](Rig& r) { return r.client.ServiceSync().error; }, true,
              [](Rig& r) { static_cast<void>(r.client.ConfigureSync(1'000, 0)); }) == 1);
}

TEST_CASE("A PDO whose configuration fails at any send is never left valid") {
  for (int k = 0; k < 20; ++k) {
    Rig r(Quick());
    REQUIRE(r.started);
    r.Bus().fail_send_index = r.Bus().sends + k;
    const auto e = r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo());
    if (e.ok()) {
      CHECK(k >= 9); // 1 invalidate, type, inhibit, timer, clear, 2 entries, count, valid = 9 sends
      CHECK_EQ(r.n5.Value(0x1800, 1), 0x185U);
      break;
    }
    CHECK((r.n5.Value(0x1800, 1) & 0x80000000U) != 0);
  }
}

TEST_CASE(
    "A fault that does not clear and a reaction that does not end time out on the drive timeout") {
  canopen_client::Config c = Quick();
  c.drive_timeout_us = 5'000;
  Rig r(c);
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Fault);
  r.n5.drive.fault_persists = true;
  CHECK(r.client.DriveResetFault(5).error == Error::DriveFault);
  r.n6.drive.fault_reaction_us = 10'000'000;
  r.n6.drive.RaiseFault(r.Bus().now_us);
  CHECK(r.client.DriveResetFault(6).error == Error::Timeout);
  Put(r.n5, SimDrive::State::NotReady);
  CHECK(r.client.DriveEnable(5).error == Error::Timeout);
  r.n6.drive.mode_latency_us = 10'000'000;
  CHECK(r.client
            .SetDriveMode(6, canopen_client::OperationMode::Homing, {},
                          canopen_client::Verify::ReadBack)
            .error == Error::DeviceFault);
}

TEST_CASE("A frame that arrives while the drive helper pauses is routed") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n6.drive.transition_delay_us = 5'000;
  // The node reports while the helper pauses between two statusword reads.
  r.Bus().Inject(Frame(0x086, {0x00, 0x31, 0x04, 0, 0, 0, 0, 0}), r.Bus().now_us + 2'500);
  CHECK(r.client.DriveEnable(6).ok());
  canopen_client::EmcyEvent e{};
  CHECK(r.client.PopEmcy(e));
  CHECK(e.node == 6 && e.code == 0x3100);
}

TEST_CASE("A guarding reply that is not a valid heartbeat is counted and the wait goes on") {
  Rig r(Quick());
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x705) {
      f.dlc = 2;
    }
  };
  CHECK(r.client.GuardNode(5).error == Error::Timeout);
  CHECK_EQ(r.client.GetStats().frames_invalid, 1U);
}

TEST_CASE("NMT stop and pre-operational commands correct the node's remembered state at once") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).ok());
  CHECK(r.client.SendNmt(NmtCommand::Stop, 5).ok());
  CHECK(r.client.GetNodeStatus(5).value.state == canopen_client::NmtState::Stopped);
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).error == Error::NodeState);
  CHECK(r.client.SendNmt(NmtCommand::PreOperational, 0).ok());
  CHECK(r.client.GetNodeStatus(5).value.state == canopen_client::NmtState::PreOperational);
  CHECK(r.client.SendNmt(NmtCommand::Start, 5).ok());
  CHECK(r.client.GetNodeStatus(5).value.state == canopen_client::NmtState::Operational);
}

TEST_CASE("A node that announces a small size and sends more cannot overrun the buffer") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585 && f.data[0] == 0x41) {
      f.data[4] = 3; // announces 3 bytes, then sends 12
    }
  };
  std::array<uint8_t, 8> buf{};
  const auto e = r.client.SdoUpload(5, 0x2100, 0, buf);
  CHECK(!e.ok() && e.error == Error::BufferTooSmall);
  CHECK_EQ(r.n5.aborts_received, 1);
  CHECK_EQ(r.n5.last_abort_code, 0x05040005U);
}

TEST_CASE("A node that announces more than it sends is told off at the last segment") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585 && f.data[0] == 0x41) {
      f.data[4] = 100;
    }
  };
  std::array<uint8_t, 128> buf{};
  const auto e = r.client.SdoUpload(5, 0x2100, 0, buf);
  CHECK(!e.ok() && e.error == Error::Protocol);
  CHECK_EQ(r.n5.last_abort_code, 0x06070010U);
}

TEST_CASE("An empty segment that is not the last is a protocol error") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585 && (f.data[0] >> 5) == 0) {
      f.data[0] = static_cast<uint8_t>((f.data[0] & 0x10U) | 0x0EU); // 7 unused bytes, not last
    }
  };
  std::array<uint8_t, 64> buf{};
  const auto e = r.client.SdoUpload(5, 0x2100, 0, buf);
  CHECK(!e.ok() && e.error == Error::Protocol);
  CHECK_EQ(r.n5.last_abort_code, 0x05040001U);
}

TEST_CASE("A node that never ends a segmented upload is cut off at the segment limit") {
  Rig r(Quick());
  REQUIRE(r.started);
  bool toggle = false;
  r.Bus().mutate = [&toggle](CanFrame& f, bool&) {
    if (f.id == 0x585 && f.data[0] != 0x41) {
      f.data[0] = static_cast<uint8_t>(toggle ? 0x10U : 0x00U); // a full segment, never the last
      toggle = !toggle;
      for (std::size_t i = 1; i < 8; ++i) {
        f.data[i] = 0x55;
      }
    }
  };
  std::vector<uint8_t> big((static_cast<std::size_t>(canopen_client::kMaxSdoSegments) + 2U) * 7U);
  const auto e = r.client.SdoUpload(5, 0x2100, 0, big);
  CHECK(!e.ok() && e.error == Error::Protocol);
  CHECK_EQ(r.n5.last_abort_code, 0x05040005U);
}

TEST_CASE("Frame builders refuse a bad node, runtime decoding agrees with the constant one") {
  CHECK(canopen_client::MakeSdoUploadRequest(0, 0x2000, 0).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoSegmentedDownload(0, 0x2000, 0, 10).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoSegmentedDownload(5, 0x2000, 0, 0).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoUploadSegmentRequest(200, false).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoAbort(0, 0x2000, 0, 1).error == Error::InvalidArgument);
  CHECK(canopen_client::ExtractBits(std::array<uint8_t, 2>{}, 0, 0).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::ExtractBits(std::array<uint8_t, 2>{}, 9, 8).error ==
        Error::InvalidArgument);
  std::array<uint8_t, 2> two{};
  CHECK(canopen_client::InsertBits(two, 9, 8, 1).error == Error::InvalidArgument);
  // Evaluated at run time: the values come from memory the compiler cannot see through.
  const volatile uint16_t words[] = {0x0000, 0x0240, 0x0231, 0x0233, 0x0237,
                                     0x0217, 0x020F, 0x0208, 0x0001};
  const canopen_client::DriveState expect[] = {canopen_client::DriveState::NotReadyToSwitchOn,
                                               canopen_client::DriveState::SwitchOnDisabled,
                                               canopen_client::DriveState::ReadyToSwitchOn,
                                               canopen_client::DriveState::SwitchedOn,
                                               canopen_client::DriveState::OperationEnabled,
                                               canopen_client::DriveState::QuickStopActive,
                                               canopen_client::DriveState::FaultReactionActive,
                                               canopen_client::DriveState::Fault,
                                               canopen_client::DriveState::Unknown};
  for (std::size_t i = 0; i < std::size(words); ++i) {
    CHECK(canopen_client::DecodeStatusword(words[i]) == expect[i]);
  }
}

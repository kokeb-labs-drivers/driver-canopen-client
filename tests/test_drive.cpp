/**
 * @file test_drive.cpp
 * @brief The CiA 402 drive helpers against the simulated drive state machine.
 */
#include <bit>
#include <cstdint>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::DriveObjects;
using canopen_client::DriveState;
using canopen_client::Error;
using canopen_client::OperationMode;
using canopen_client::Verify;
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

} // namespace

TEST_CASE("The statusword and the state decode, bits outside the state masks are ignored") {
  Rig r;
  REQUIRE(r.started);
  const auto sw = r.client.ReadStatusword(5);
  CHECK(sw.ok() && sw.value == 0x02C0U); // Switch on disabled plus the remote and warning bits
  const auto st = r.client.ReadDriveState(5);
  CHECK(st.ok() && st.value == DriveState::SwitchOnDisabled);
  Put(r.n5, SimDrive::State::Fault);
  CHECK(r.client.ReadDriveState(5).value == DriveState::Fault);
  Put(r.n5, SimDrive::State::Enabled);
  CHECK(r.client.ReadDriveState(5).value == DriveState::OperationEnabled);
}

TEST_CASE("DriveEnable walks Switch on disabled to Operation enabled in three writes") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.DriveEnable(5).ok());
  CHECK(r.n5.drive.state == SimDrive::State::Enabled);
  CHECK_EQ(r.n5.drive.writes, 3);
  CHECK_EQ(r.n5.drive.controlword, 0x000F);
  CHECK(r.client.DriveEnable(5).ok()); // already enabled: no writes
  CHECK_EQ(r.n5.drive.writes, 3);
}

TEST_CASE("DriveEnable continues from a state part way and waits for slow transitions") {
  Rig r(Quick());
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Ready);
  CHECK(r.client.DriveEnable(5).ok());
  CHECK_EQ(r.n5.drive.writes, 2);
  r.n6.drive.transition_delay_us = 5'000;
  CHECK(r.client.DriveEnable(6).ok());
  CHECK(r.n6.drive.state == SimDrive::State::Enabled);
}

TEST_CASE("DriveEnable from Quick stop active disables voltage first") {
  Rig r;
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::QuickStop);
  CHECK(r.client.DriveEnable(5).ok());
  CHECK_EQ(r.n5.drive.writes, 4); // disable voltage, shutdown, switch on, enable operation
}

TEST_CASE("DriveEnable does not clear a fault and gives up on a drive that never gets ready") {
  Rig r(Quick());
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Fault);
  const auto f = r.client.DriveEnable(5);
  CHECK(!f.ok() && f.error == Error::DriveFault);
  CHECK_EQ(r.n5.drive.writes, 0);
  Put(r.n6, SimDrive::State::NotReady);
  const uint64_t t0 = r.Bus().now_us;
  const auto t = r.client.DriveEnable(6);
  CHECK(!t.ok() && t.error == Error::Timeout);
  CHECK(r.Bus().now_us - t0 >= 200'000U); // drive_timeout_us, not a step count
  CHECK(r.Bus().now_us - t0 < 260'000U);
  CHECK_EQ(r.n6.drive.writes, 0);
}

TEST_CASE("DriveEnable stops at the iteration bound and reports transport errors") {
  canopen_client::Config c = Quick();
  c.drive_timeout_us = 100'000;
  Rig r(c);
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::NotReady);
  const std::size_t mark = r.Sent();
  CHECK(r.client.DriveEnable(5).error == Error::Timeout);
  const std::size_t reads = r.SentWithId(mark, 0x605);
  CHECK(reads >= 50U &&
        reads <= 102U); // timeout / poll interval + 2, whatever the SDO round trips cost
  r.n6.responds = false;
  CHECK(r.client.DriveEnable(6).error == Error::Timeout);
  r.Bus().send_ok = false;
  CHECK(r.client.DriveEnable(5).error == Error::Bus);
}

TEST_CASE("A write failing half way through DriveEnable is reported") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.Bus().fail_send_index =
      r.Bus().sends + 1; // the statusword read goes out, the first write fails
  CHECK(r.client.DriveEnable(5).error == Error::Bus);
  r.Bus().fail_send_index = -1;
  CHECK(r.client.DriveEnable(5).ok());
}

TEST_CASE("DriveDisable and DriveQuickStop write their controlword from any state") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.DriveEnable(5).ok());
  CHECK(r.client.DriveQuickStop(5).ok());
  CHECK_EQ(r.n5.drive.controlword, 0x0002);
  CHECK(r.n5.drive.state == SimDrive::State::QuickStop);
  CHECK(r.client.DriveDisable(5).ok());
  CHECK_EQ(r.n5.drive.controlword, 0x0000);
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
}

TEST_CASE("Stop commands are retried until the node acknowledges, and then give up") {
  Rig r(Quick());
  REQUIRE(r.started);
  CHECK(r.client.DriveEnable(5).ok());
  int dropped = 0;
  r.Bus().mutate = [&dropped](CanFrame&, bool& drop) {
    if (dropped < 2) {
      drop = true;
      ++dropped;
    }
  };
  const int before = r.n5.sdo_requests;
  CHECK(r.client.DriveDisable(5).ok()); // the third of three attempts is answered
  CHECK_EQ(r.n5.sdo_requests - before, 3);
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
  CHECK(r.client.DriveEnable(5).ok());
  r.Bus().mutate = [](CanFrame&, bool& drop) { drop = true; };
  const int before2 = r.n5.sdo_requests;
  const auto e = r.client.DriveQuickStop(5);
  CHECK(!e.ok() && e.error == Error::Timeout);
  CHECK_EQ(r.n5.sdo_requests - before2, 3);
  // Every attempt is a write the node acted on, although no answer arrived.
  CHECK(r.n5.drive.state == SimDrive::State::QuickStop);
}

TEST_CASE("The safety attempts count is the configured one") {
  canopen_client::Config c = Quick();
  c.safety_attempts = 5;
  Rig r(c);
  REQUIRE(r.started);
  r.Bus().mutate = [](CanFrame&, bool& drop) { drop = true; };
  std::size_t mark = r.Sent();
  CHECK(r.client.DriveDisable(5).error == Error::Timeout);
  CHECK_EQ(r.SentWithId(mark, 0x605), 5U);
  c.safety_attempts = 1;
  CHECK(r.client.SetConfig(c).ok());
  mark = r.Sent();
  CHECK(r.client.DriveDisable(5).error == Error::Timeout);
  CHECK_EQ(r.SentWithId(mark, 0x605), 1U);
}

TEST_CASE("A node the heartbeat calls Stopped blocks enabling but never a stop command") {
  Rig r(Quick());
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x04}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).ok());
  const std::size_t mark = r.Sent();
  CHECK(r.client.DriveEnable(5).error == Error::NodeState);
  CHECK(r.client.ReadStatusword(5).error == Error::NodeState);
  CHECK_EQ(r.Sent(), mark);
  CHECK(r.client.DriveDisable(5).ok());
  CHECK(r.client.DriveQuickStop(5).ok());
  CHECK(r.Sent() > mark);
}

TEST_CASE("DriveResetFault gives bit 7 a rising edge and waits for the drive to leave Fault") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.drive.RaiseFault(r.Bus().now_us);
  CHECK(r.client.DriveResetFault(5).ok()); // waits out Fault reaction active first
  CHECK(r.n5.drive.state == SimDrive::State::Disabled);
  CHECK_EQ(r.n5.drive.writes, 3); // 0x0000, 0x0080 (the edge), then 0x0000 again: bit 7 left low
  CHECK_EQ(r.n5.drive.controlword, 0x0000);
}

TEST_CASE("DriveResetFault reports a persistent fault and leaves a healthy drive alone") {
  Rig r(Quick());
  REQUIRE(r.started);
  Put(r.n5, SimDrive::State::Fault);
  r.n5.drive.fault_persists = true;
  const auto p = r.client.DriveResetFault(5);
  CHECK(!p.ok() && p.error == Error::DriveFault);
  CHECK(r.client.DriveResetFault(6).ok()); // Switch on disabled: not a fault
  CHECK_EQ(r.n6.drive.writes, 0);
}

TEST_CASE("DriveResetFault gives up on a reaction that never ends and reports transport errors") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.drive.fault_reaction_us = 10'000'000;
  r.n5.drive.RaiseFault(r.Bus().now_us);
  CHECK(r.client.DriveResetFault(5).error == Error::Timeout);
  r.n6.responds = false;
  CHECK(r.client.DriveResetFault(6).error == Error::Timeout);
}

TEST_CASE("The mode of operation is written and read back, with Verify only when asked") {
  Rig r(Quick());
  REQUIRE(r.started);
  std::size_t mark = r.Sent();
  CHECK(r.client.SetDriveMode(5, OperationMode::CyclicSynchronousPosition).ok());
  CHECK_EQ(r.SentWithId(mark, 0x605), 1U); // no read-back by default
  CHECK_EQ(r.n5.drive.mode, 8);
  CHECK(r.client.ReadDriveMode(5).value == OperationMode::CyclicSynchronousPosition);
  r.n5.drive.mode_latency_us = 10'000;
  mark = r.Sent();
  CHECK(r.client.SetDriveMode(5, OperationMode::ProfileVelocity, {}, Verify::ReadBack).ok());
  CHECK(r.SentWithId(mark, 0x605) > 2U); // polled until the display changed
  CHECK(r.client.ReadDriveMode(5).value == OperationMode::ProfileVelocity);
}

TEST_CASE("A mode the display never shows is a device fault") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.drive.mode_latency_us = 10'000'000;
  const auto e = r.client.SetDriveMode(5, OperationMode::Homing, {}, Verify::ReadBack);
  CHECK(!e.ok() && e.error == Error::DeviceFault);
  CHECK(r.client.SetDriveMode(5, std::bit_cast<OperationMode>(static_cast<int8_t>(-1)))
            .ok()); // manufacturer-specific
  CHECK_EQ(r.n5.drive.mode, -1);
  r.n6.responds = false;
  CHECK(r.client.SetDriveMode(6, OperationMode::Homing, {}, Verify::ReadBack).error ==
        Error::Timeout);
  CHECK(r.client.ReadDriveMode(6).error == Error::Timeout);
}

TEST_CASE("Drive objects can be remapped, and an index below 0x1000 is refused before sending") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.n5.MoveDrive(0x800);
  const DriveObjects second{0x6840, 0x6841, 0x6860, 0x6861};
  CHECK(r.client.DriveEnable(5, second).ok());
  CHECK(r.n5.drive.state == SimDrive::State::Enabled);
  CHECK(r.client.SetDriveMode(5, OperationMode::Velocity, second, Verify::ReadBack).ok());
  CHECK(r.client.ReadDriveMode(5, second).value == OperationMode::Velocity);
  const auto e = r.client.ReadStatusword(5); // the default objects are gone from this node
  CHECK(!e.ok() && e.error == Error::SdoAbort);
  const std::size_t mark = r.Sent();
  const DriveObjects low{0x0FFF, 0x6041, 0x6060, 0x6061};
  const DriveObjects low2{0x6040, 0x0FFF, 0x6060, 0x6061};
  const DriveObjects low3{0x6040, 0x6041, 0x0, 0x6061};
  const DriveObjects low4{0x6040, 0x6041, 0x6060, 0x1};
  for (const DriveObjects& o : {low, low2, low3, low4}) {
    CHECK(r.client.ReadStatusword(5, o).error == Error::InvalidArgument);
    CHECK(r.client.ReadDriveState(5, o).error == Error::InvalidArgument);
    CHECK(r.client.DriveEnable(5, o).error == Error::InvalidArgument);
    CHECK(r.client.DriveDisable(5, o).error == Error::InvalidArgument);
    CHECK(r.client.DriveQuickStop(5, o).error == Error::InvalidArgument);
    CHECK(r.client.DriveResetFault(5, o).error == Error::InvalidArgument);
    CHECK(r.client.SetDriveMode(5, OperationMode::Velocity, o).error == Error::InvalidArgument);
    CHECK(r.client.ReadDriveMode(5, o).error == Error::InvalidArgument);
  }
  CHECK_EQ(r.Sent(), mark);
}

TEST_CASE("Drive calls with a bad node send nothing") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  CHECK(r.client.ReadStatusword(0).error == Error::InvalidArgument);
  CHECK(r.client.DriveEnable(128).error == Error::InvalidArgument);
  CHECK(r.client.DriveDisable(0).error == Error::InvalidArgument);
  CHECK(r.client.DriveQuickStop(200).error == Error::InvalidArgument);
  CHECK(r.client.DriveResetFault(0).error == Error::InvalidArgument);
  CHECK(r.client.SetDriveMode(0, OperationMode::Velocity).error == Error::InvalidArgument);
  CHECK(r.client.ReadDriveMode(255).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
}

TEST_CASE("A statusword that is no state stops the walk as a drive fault") {
  Rig r(Quick());
  REQUIRE(r.started);
  r.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585 && f.data[1] == 0x41 && f.data[2] == 0x60) {
      f.data[4] = 0x0B; // 0x0B & 0x4F = 0x0B: matches no state
      f.data[5] = 0x00;
    }
  };
  const auto e = r.client.DriveEnable(5);
  CHECK(!e.ok() && e.error == Error::DriveFault);
  CHECK(r.client.ReadDriveState(5).value == DriveState::Unknown);
}

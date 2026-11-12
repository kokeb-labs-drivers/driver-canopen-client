/**
 * @file test_lifecycle.cpp
 * @brief Start-up, shutdown, configuration, the transmit-only port, the optional Log hook, error
 * coverage.
 */
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Client;
using canopen_client::Config;
using canopen_client::Error;
using canopen_client::NmtCommand;
using namespace canopen_client_test;

static_assert(canopen_client::CanPort<TxOnlyPort> && !canopen_client::CanRxPort<TxOnlyPort>);
static_assert(canopen_client::CanRxPort<MockPort> && canopen_client::CanRxPort<LoggingPort>);

TEST_CASE("Nothing touches the port before EnsureInitialized succeeds") {
  MockPort port;
  port.bus.AddNode(5);
  Client<MockPort> c(port);
  CHECK(!c.IsInitialized());
  CHECK_EQ(port.bus.init_calls, 0);
  std::array<uint8_t, 4> buf{};
  const std::array<uint8_t, 2> two{1, 2};
  CHECK(c.SendNmt(NmtCommand::Start, 5).error == Error::NotInitialized);
  CHECK(c.SendSync().error == Error::NotInitialized);
  CHECK(c.SendRpdo(0x205, two).error == Error::NotInitialized);
  CHECK(c.SdoDownloadUnconfirmed(5, 0x2000, 0, two).error == Error::NotInitialized);
  CHECK(c.Poll(0).error == Error::NotInitialized);
  CHECK(c.MonitorNode(5, 10'000).error == Error::NotInitialized);
  CHECK(c.UnmonitorNode(5).error == Error::NotInitialized);
  CHECK(c.GetNodeStatus(5).error == Error::NotInitialized);
  CHECK(c.GuardNode(5).error == Error::NotInitialized);
  CHECK(c.SubscribeTpdo(0x185, 2).error == Error::NotInitialized);
  CHECK(c.UnsubscribeTpdo(0x185).error == Error::NotInitialized);
  CHECK(c.ReadTpdo(0x185).error == Error::NotInitialized);
  CHECK(c.SdoUpload(5, 0x2000, 0, buf).error == Error::NotInitialized);
  CHECK(c.SdoDownload(5, 0x2000, 0, two).error == Error::NotInitialized);
  CHECK(c.SdoReadU8(5, 0x2002, 0).error == Error::NotInitialized);
  CHECK(c.SdoReadU16(5, 0x2001, 0).error == Error::NotInitialized);
  CHECK(c.SdoReadU32(5, 0x2000, 0).error == Error::NotInitialized);
  CHECK(c.SdoWriteU8(5, 0x2002, 0, 1).error == Error::NotInitialized);
  CHECK(c.SdoWriteU16(5, 0x2001, 0, 1).error == Error::NotInitialized);
  CHECK(c.SdoWriteU32(5, 0x2000, 0, 1).error == Error::NotInitialized);
  CHECK(c.ReadDeviceType(5).error == Error::NotInitialized);
  CHECK(c.ReadErrorRegister(5).error == Error::NotInitialized);
  CHECK(c.WriteHeartbeatProducerTime(5, 10).error == Error::NotInitialized);
  CHECK(c.StoreParameters(5, 1).error == Error::NotInitialized);
  CHECK(c.RestoreDefaults(5, 1).error == Error::NotInitialized);
  CHECK(c.ConfigureSync(1'000, 0).error == Error::NotInitialized);
  CHECK(c.ServiceSync().error == Error::NotInitialized);
  CHECK(c.ConfigurePdo(5, canopen_client::PdoKind::Transmit, 1, {}).error == Error::NotInitialized);
  CHECK(c.DisablePdo(5, canopen_client::PdoKind::Transmit, 1).error == Error::NotInitialized);
  CHECK(c.ReadPdoConfig(5, canopen_client::PdoKind::Transmit, 1).error == Error::NotInitialized);
  CHECK(c.ReadStatusword(5).error == Error::NotInitialized);
  CHECK(c.ReadDriveState(5).error == Error::NotInitialized);
  CHECK(c.DriveEnable(5).error == Error::NotInitialized);
  CHECK(c.DriveDisable(5).error == Error::NotInitialized);
  CHECK(c.DriveQuickStop(5).error == Error::NotInitialized);
  CHECK(c.DriveResetFault(5).error == Error::NotInitialized);
  CHECK(c.SetDriveMode(5, canopen_client::OperationMode::Velocity).error == Error::NotInitialized);
  CHECK(c.ReadDriveMode(5).error == Error::NotInitialized);
  CHECK_EQ(port.bus.sends, 0);
  CHECK_EQ(port.bus.receives, 0);
  CHECK_EQ(port.bus.init_calls, 0);
}

TEST_CASE("Before start-up NotInitialized wins over an invalid argument") {
  MockPort port;
  Client<MockPort> c(port);
  const canopen_client::DriveObjects low{0x0001, 0x0002, 0x0003, 0x0004};
  CHECK(c.StoreParameters(5, 9).error == Error::NotInitialized);
  CHECK(c.RestoreDefaults(0, 0).error == Error::NotInitialized);
  CHECK(c.DisablePdo(5, canopen_client::PdoKind::Transmit, 0).error == Error::NotInitialized);
  CHECK(c.ReadPdoConfig(5, canopen_client::PdoKind::Transmit, 0).error == Error::NotInitialized);
  CHECK(c.ReadStatusword(5, low).error == Error::NotInitialized);
  CHECK(c.DriveEnable(5, low).error == Error::NotInitialized);
  CHECK(c.DriveDisable(5, low).error == Error::NotInitialized);
  CHECK(c.DriveQuickStop(5, low).error == Error::NotInitialized);
  CHECK(c.DriveResetFault(5, low).error == Error::NotInitialized);
  CHECK(c.SetDriveMode(5, canopen_client::OperationMode::Velocity, low).error ==
        Error::NotInitialized);
  CHECK(c.ReadDriveMode(5, low).error == Error::NotInitialized);
}

TEST_CASE("EnsureInitialized starts the port once and a failure leaves the driver stopped") {
  MockPort port;
  port.bus.init_ok = false;
  Client<MockPort> c(port);
  CHECK(c.EnsureInitialized().error == Error::Bus);
  CHECK(!c.IsInitialized());
  CHECK(c.SendSync().error == Error::NotInitialized);
  port.bus.init_ok = true;
  CHECK(c.EnsureInitialized().ok());
  CHECK(c.IsInitialized());
  CHECK(c.EnsureInitialized().ok());
  CHECK(c.EnsureInitialized().ok());
  CHECK_EQ(port.bus.init_calls, 2); // the failed start and the one that worked, no more
}

TEST_CASE("An invalid stored configuration is refused before the port is touched") {
  MockPort port;
  Config bad{};
  bad.sdo_timeout_us = 0;
  Client<MockPort> c(port, bad);
  CHECK(c.EnsureInitialized().error == Error::InvalidArgument);
  CHECK_EQ(port.bus.init_calls, 0);
  CHECK(c.SetConfig(Config{}).ok());
  CHECK(c.EnsureInitialized().ok());
}

TEST_CASE("Deinitialize forgets everything that was received, with no traffic") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  CHECK(r.client.SubscribeTpdo(0x185, 2).ok());
  r.Bus().Emcy(5, 0x1000, 1);
  CHECK(r.client.Poll(5'000).ok());
  CHECK(!r.client.SdoReadU32(5, 0x2003, 1).ok()); // an abort is remembered
  CHECK(r.client.GetLastAbort().code != 0);
  const std::size_t mark = r.Sent();
  CHECK(r.client.Deinitialize().ok());
  CHECK(!r.client.IsInitialized());
  CHECK_EQ(r.Sent(), mark);
  CHECK(r.client.SendSync().error == Error::NotInitialized);
  CHECK(r.client.EnsureInitialized().ok());
  CHECK(r.client.GetNodeStatus(5).error == Error::InvalidArgument);
  CHECK(r.client.ReadTpdo(0x185).error == Error::InvalidArgument);
  canopen_client::EmcyEvent e{};
  CHECK(!r.client.PopEmcy(e));
  CHECK_EQ(r.client.GetLastAbort().code, 0U);
  CHECK_EQ(r.client.GetStats().frames_received, 0U);
}

TEST_CASE("SetConfig keeps the old configuration when the new one fails Validate") {
  Rig r;
  REQUIRE(r.started);
  Config c{};
  c.sdo_timeout_us = 123'000;
  CHECK(r.client.SetConfig(c).ok());
  CHECK_EQ(r.client.GetConfig().sdo_timeout_us, 123'000U);
  Config bad = c;
  bad.safety_attempts = 0;
  CHECK(r.client.SetConfig(bad).error == Error::InvalidArgument);
  CHECK_EQ(r.client.GetConfig().sdo_timeout_us, 123'000U);
  CHECK_EQ(r.client.GetConfig().safety_attempts, 3);
}

TEST_CASE("Validate checks every field at both ends of its range") {
  using canopen_client::Validate;
  Config c{};
  CHECK(Validate(c) == Error::Ok);
  c.sdo_timeout_us = 999;
  CHECK(Validate(c) == Error::InvalidArgument);
  c.sdo_timeout_us = 1'000;
  CHECK(Validate(c) == Error::Ok);
  c.sdo_timeout_us = 60'000'000;
  CHECK(Validate(c) == Error::Ok);
  c.sdo_timeout_us = 60'000'001;
  CHECK(Validate(c) == Error::InvalidArgument);
  c = Config{};
  c.guard_timeout_us = 999;
  CHECK(Validate(c) == Error::InvalidArgument);
  c.guard_timeout_us = 10'000'000;
  CHECK(Validate(c) == Error::Ok);
  c.guard_timeout_us = 10'000'001;
  CHECK(Validate(c) == Error::InvalidArgument);
  c = Config{};
  c.drive_timeout_us = 999;
  CHECK(Validate(c) == Error::InvalidArgument);
  c.drive_timeout_us = 60'000'001;
  CHECK(Validate(c) == Error::InvalidArgument);
  c = Config{};
  c.drive_poll_interval_us = 99;
  CHECK(Validate(c) == Error::InvalidArgument);
  c.drive_poll_interval_us = 1'000'000;
  CHECK(Validate(c) == Error::Ok);
  c.drive_poll_interval_us = 1'000'001;
  CHECK(Validate(c) == Error::InvalidArgument);
  c = Config{};
  c.safety_attempts = 0;
  CHECK(Validate(c) == Error::InvalidArgument);
  c.safety_attempts = 8;
  CHECK(Validate(c) == Error::Ok);
  c.safety_attempts = 9;
  CHECK(Validate(c) == Error::InvalidArgument);
  CHECK(Validate(canopen_client::FastBusPreset()) == Error::Ok);
  CHECK(Validate(canopen_client::SlowBusPreset()) == Error::Ok);
}

TEST_CASE("A transmit-only port sends NMT, SYNC, RPDO and an unconfirmed write, and nothing else") {
  TxOnlyPort port;
  Client<TxOnlyPort> c(port);
  CHECK(c.SendNmt(NmtCommand::Start, 5).error == Error::NotInitialized);
  REQUIRE(c.EnsureInitialized().ok());
  CHECK(c.SendNmt(NmtCommand::Start, 5).ok());
  CHECK(c.SendSync(2).ok());
  const std::array<uint8_t, 2> pdo{0x34, 0x12};
  CHECK(c.SendRpdo(0x205, pdo).ok());
  const std::array<uint8_t, 2> value{0x34, 0x12};
  CHECK(c.SdoDownloadUnconfirmed(5, 0x2001, 0, value).ok());
  REQUIRE(port.frames.size() == 4);
  CHECK(port.frames[0].id == 0x000 && port.frames[0].data[0] == 0x01 &&
        port.frames[0].data[1] == 5);
  CHECK(port.frames[1].id == 0x080 && port.frames[1].dlc == 1 && port.frames[1].data[0] == 2);
  CHECK(port.frames[2].id == 0x205 && port.frames[2].dlc == 2 && port.frames[2].data[1] == 0x12);
  const CanFrame& w = port.frames[3];
  CHECK(w.id == 0x605 && w.dlc == 8 && w.data[0] == 0x2B && w.data[1] == 0x01 && w.data[2] == 0x20);
  CHECK(w.data[3] == 0 && w.data[4] == 0x34 && w.data[5] == 0x12);
  CHECK(c.Deinitialize().ok());
  CHECK(c.SendSync().error == Error::NotInitialized);
}

TEST_CASE("A transmit-only port's arguments are checked and its send failures reported") {
  TxOnlyPort port;
  Client<TxOnlyPort> c(port);
  port.init_ok = false;
  CHECK(c.EnsureInitialized().error == Error::Bus);
  port.init_ok = true;
  REQUIRE(c.EnsureInitialized().ok());
  const std::array<uint8_t, 5> five{};
  const std::array<uint8_t, 2> two{};
  CHECK(c.SdoDownloadUnconfirmed(0, 0x2000, 0, two).error == Error::InvalidArgument);
  CHECK(c.SdoDownloadUnconfirmed(128, 0x2000, 0, two).error == Error::InvalidArgument);
  CHECK(c.SdoDownloadUnconfirmed(5, 0x2000, 0, std::span<const uint8_t>{}).error ==
        Error::InvalidArgument);
  CHECK(c.SdoDownloadUnconfirmed(5, 0x2000, 0, five).error == Error::InvalidArgument);
  CHECK(c.SendRpdo(0x7FF, two).error == Error::InvalidArgument);
  CHECK(c.SendSync(241).error == Error::InvalidArgument);
  CHECK(c.SendNmt(NmtCommand::Start, 200).error == Error::InvalidArgument);
  CHECK(port.frames.empty());
  port.send_ok = false;
  CHECK(c.SendNmt(NmtCommand::Start, 5).error == Error::Bus);
  CHECK(c.SendSync().error == Error::Bus);
  CHECK(c.SendRpdo(0x205, two).error == Error::Bus);
  CHECK(c.SdoDownloadUnconfirmed(5, 0x2000, 0, two).error == Error::Bus);
}

TEST_CASE("Every call that takes arguments refuses a bad one without sending a frame") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  std::array<uint8_t, 8> buf{};
  const std::array<uint8_t, 4> four{};
  CHECK(r.client.SdoUpload(0, 0x2000, 0, buf).error == Error::InvalidArgument);
  CHECK(r.client.SdoUpload(128, 0x2000, 0, buf).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownload(0, 0x2000, 0, four).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownload(5, 0x2000, 0, std::span<const uint8_t>{}).error ==
        Error::InvalidArgument);
  CHECK(r.client.SdoReadU8(0, 0x2002, 0).error == Error::InvalidArgument);
  CHECK(r.client.SdoReadU16(0, 0x2001, 0).error == Error::InvalidArgument);
  CHECK(r.client.SdoReadU32(0, 0x2000, 0).error == Error::InvalidArgument);
  CHECK(r.client.SdoWriteU8(0, 0x2002, 0, 1).error == Error::InvalidArgument);
  CHECK(r.client.SdoWriteU16(0, 0x2001, 0, 1).error == Error::InvalidArgument);
  CHECK(r.client.SdoWriteU32(0, 0x2000, 0, 1).error == Error::InvalidArgument);
  CHECK(r.client.ReadDeviceType(0).error == Error::InvalidArgument);
  CHECK(r.client.ReadErrorRegister(0).error == Error::InvalidArgument);
  CHECK(r.client.WriteHeartbeatProducerTime(0, 10).error == Error::InvalidArgument);
  CHECK(r.client.StoreParameters(0, 1).error == Error::InvalidArgument);
  CHECK(r.client.StoreParameters(5, 0).error == Error::InvalidArgument);
  CHECK(r.client.StoreParameters(5, 5).error == Error::InvalidArgument);
  CHECK(r.client.RestoreDefaults(0, 1).error == Error::InvalidArgument);
  CHECK(r.client.RestoreDefaults(5, 0).error == Error::InvalidArgument);
  CHECK(r.client.RestoreDefaults(5, 5).error == Error::InvalidArgument);
  CHECK(r.client.SendNmt(NmtCommand::Start, 128).error == Error::InvalidArgument);
  CHECK(r.client.SendSync(241).error == Error::InvalidArgument);
  CHECK(r.client.SendRpdo(0x100, four).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownloadUnconfirmed(0, 0x2000, 0, four).error == Error::InvalidArgument);
  CHECK(r.client.GuardNode(0).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(0, 10'000).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(1, 0).error == Error::InvalidArgument);
  CHECK(r.client.SubscribeTpdo(1, 1).error == Error::InvalidArgument);
  CHECK(r.client.ReadPdoConfig(5, canopen_client::PdoKind::Receive, 0).error ==
        Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
  CHECK_EQ(r.Bus().receives, 0);
}

TEST_CASE("The optional Log hook receives a line when the port refuses a frame") {
  LoggingPort port;
  Client<LoggingPort> c(port);
  REQUIRE(c.EnsureInitialized().ok());
  CHECK(port.inner.bus.logs.empty());
  port.inner.bus.send_ok = false;
  CHECK(c.SendSync().error == Error::Bus);
  CHECK(!port.inner.bus.logs.empty());
}

TEST_CASE("Every error the driver returns has a name, and the ones it never returns are named so") {
  using canopen_client::ToString;
  const Error all[] = {Error::Ok,
                       Error::NotInitialized,
                       Error::InvalidArgument,
                       Error::Bus,
                       Error::Timeout,
                       Error::DeviceIdMismatch,
                       Error::Crc,
                       Error::Busy,
                       Error::DeviceFault,
                       Error::Unsupported,
                       Error::SdoAbort,
                       Error::Protocol,
                       Error::BufferTooSmall,
                       Error::InvalidFrame,
                       Error::NodeState,
                       Error::DriveFault};
  for (std::size_t i = 0; i < std::size(all); ++i) {
    CHECK(std::string_view(ToString(all[i])) != "Unknown");
    for (std::size_t j = i + 1; j < std::size(all); ++j) {
      CHECK(std::string_view(ToString(all[i])) != std::string_view(ToString(all[j])));
    }
  }
  CHECK(std::string_view(ToString(static_cast<Error>(12))) == "Unknown");
  CHECK(std::string_view(ToString(static_cast<Error>(200))) == "Unknown");
  CHECK_EQ(static_cast<int>(Error::SdoAbort), 16);
  CHECK_EQ(static_cast<int>(Error::DriveFault), 21);
}

TEST_CASE("Each error the driver can return is returned by some call") {
  using canopen_client::ToString;
  Rig r(canopen_client::FastBusPreset());
  REQUIRE(r.started);
  std::array<uint8_t, 1> tiny{};
  r.n5.drive.state = SimDrive::State::Fault;
  r.n5.drive.pending = SimDrive::State::Fault;
  r.n5.drive.mode_latency_us = 10'000'000;
  bool seen[32] = {};
  const auto mark = [&seen](Error e) { seen[static_cast<unsigned>(e)] = true; };
  mark(r.client.SdoReadU32(5, 0x2000, 0).error);      // Ok
  mark(r.client.SdoReadU32(0, 0x2000, 0).error);      // InvalidArgument
  mark(r.client.SdoReadU32(5, 0x6666, 0).error);      // SdoAbort
  mark(r.client.SdoReadU16(5, 0x2000, 0).error);      // Protocol
  mark(r.client.SdoUpload(5, 0x2100, 0, tiny).error); // BufferTooSmall
  mark(r.client.DriveEnable(5).error);                // DriveFault
  mark(r.client
           .SetDriveMode(5, canopen_client::OperationMode::Homing, {},
                         canopen_client::Verify::ReadBack)
           .error);
  r.n6.responds = false;
  mark(r.client.SdoReadU32(6, 0x2000, 0).error); // Timeout
  r.Bus().send_ok = false;
  mark(r.client.SendSync().error); // Bus
  r.Bus().send_ok = true;
  r.n5.Find(0x1800, 1)->value = Le(0x20000185U, 4);
  mark(r.client.ReadPdoConfig(5, canopen_client::PdoKind::Transmit, 1).error); // Unsupported
  CHECK(r.client.MonitorNode(5, 1'000'000).ok());
  r.Bus().Inject(Frame(0x705, {0x04}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).ok());
  mark(r.client.SdoReadU32(5, 0x2000, 0).error); // NodeState
  CHECK(r.client.Deinitialize().ok());
  mark(r.client.SendSync().error);                              // NotInitialized
  mark(canopen_client::ParseHeartbeat(Frame(0x705, {})).error); // InvalidFrame
  for (const Error e :
       {Error::Ok, Error::NotInitialized, Error::InvalidArgument, Error::Bus, Error::Timeout,
        Error::DeviceFault, Error::Unsupported, Error::SdoAbort, Error::Protocol,
        Error::BufferTooSmall, Error::InvalidFrame, Error::NodeState, Error::DriveFault}) {
    if (!seen[static_cast<unsigned>(e)]) {
      std::printf("    not returned: %s\n", ToString(e));
    }
    CHECK(seen[static_cast<unsigned>(e)]);
  }
  CHECK(!seen[static_cast<unsigned>(Error::DeviceIdMismatch)]);
  CHECK(!seen[static_cast<unsigned>(Error::Crc)]);
  CHECK(!seen[static_cast<unsigned>(Error::Busy)]);
}

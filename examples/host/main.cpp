/**
 * @file main.cpp
 * @brief The CANopen client driver on your PC: start a simulated drive node, configure a PDO,
 * enable the drive and watch an emergency message arrive.
 *
 * No hardware and no SDK: the host port (host_canopen_client_port.hpp) connects the real driver to
 * the bus model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <array>
#include <cstdint>
#include <cstdio>

#include "canopen_client.hpp"
#include "host_canopen_client_port.hpp"

namespace {

/// Print a failed step and tell the caller to stop.
template <typename R>
bool Step(const char* what, const R& result) {
  if (!result) {
    std::printf("%s failed: %s\n", what, canopen_client::ToString(result.error));
    return false;
  }
  return true;
}

} // namespace

int main() {
  HostCanopenClientPort port;
  canopen_client_test::SimNode& node = port.bus.AddNode(5);
  canopen_client::Client<HostCanopenClientPort> client(port); // no I/O yet
  constexpr uint8_t kNode = 5;

  std::printf("canopen_client driver %s on the host\n", canopen_client::GetDriverVersion());
  if (!Step("EnsureInitialized", client.EnsureInitialized())) {
    return 1;
  }
  const auto type = client.ReadDeviceType(kNode);
  if (!Step("ReadDeviceType", type)) {
    return 1;
  }
  std::printf("node %u: device type 0x%08X\n", static_cast<unsigned>(kNode),
              static_cast<unsigned>(type.value));

  // A transmit PDO of the position and the statusword, sent on every SYNC.
  canopen_client::PdoConfig pdo{};
  pdo.cob_id = 0x185;
  pdo.transmission_type = 1;
  pdo.entry_count = 2;
  pdo.entries[0] = {.index = 0x2001, .sub = 0, .bits = 16};
  pdo.entries[1] = {.index = 0x2002, .sub = 0, .bits = 8};
  if (!Step("ConfigurePdo",
            client.ConfigurePdo(kNode, canopen_client::PdoKind::Transmit, 1, pdo)) ||
      !Step("SubscribeTpdo", client.SubscribeTpdo(0x185, 3)) ||
      !Step("SendNmt", client.SendNmt(canopen_client::NmtCommand::Start, kNode)) ||
      !Step("SendSync", client.SendSync())) {
    return 1;
  }
  const auto frame = client.Poll(10'000);
  if (!Step("Poll", frame) || (frame.value != canopen_client::FrameClass::Pdo)) {
    std::printf("the TPDO did not arrive\n");
    return 1;
  }
  const auto sample = client.ReadTpdo(0x185);
  if (!Step("ReadTpdo", sample)) {
    return 1;
  }
  std::printf("TPDO 0x185: %02X %02X %02X\n", static_cast<unsigned>(sample.value.data[0]),
              static_cast<unsigned>(sample.value.data[1]),
              static_cast<unsigned>(sample.value.data[2]));

  // The drive: select a mode, walk the state machine to Operation enabled, stop it again.
  if (!Step("SetDriveMode",
            client.SetDriveMode(kNode, canopen_client::OperationMode::ProfileVelocity)) ||
      !Step("DriveEnable", client.DriveEnable(kNode))) {
    return 1;
  }
  const auto state = client.ReadDriveState(kNode);
  if (!Step("ReadDriveState", state)) {
    return 1;
  }
  std::printf("drive: %s\n", canopen_client::ToString(state.value));

  port.bus.Emcy(kNode, 0x2310, 0x02); // the node reports an over-current
  canopen_client::EmcyEvent event{};
  if (!Step("Poll", client.Poll(10'000)) || !client.PopEmcy(event)) {
    std::printf("no emergency message\n");
    return 1;
  }
  std::printf("EMCY from node %u: code 0x%04X\n", static_cast<unsigned>(event.node),
              static_cast<unsigned>(event.code));
  if (!Step("DriveDisable", client.DriveDisable(kNode))) {
    return 1;
  }
  return (node.drive.state == canopen_client_test::SimDrive::State::Disabled) ? 0 : 1;
}

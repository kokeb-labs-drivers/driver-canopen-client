/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <array>
#include <cstdint>

#include "canopen_client.hpp"
#include "my_port.hpp"

using Client = canopen_client::Client<MyPort>;

// --8<-- [start:sdo]
canopen_client::Error ConfigureNode(Client& client) {
  // Expedited for 1 to 4 bytes, segmented for more; the driver picks.
  if (auto r = client.SdoWriteU16(5, 0x2001, 0, 1234); !r) {
    return r.error; // SdoAbort: the node refused, `client.GetLastAbort()` says why
  }
  const auto value = client.SdoReadU32(5, 0x2000, 0);
  if (!value) {
    return value.error;
  }
  std::array<uint8_t, 64> name{};
  const auto n = client.SdoUpload(5, 0x1008, 0, name); // device name: a segmented read
  return n.error; // BufferTooSmall if the node has more than 64 bytes
}
// --8<-- [end:sdo]

// --8<-- [start:monitor]
canopen_client::Error WatchNode(Client& client) {
  if (auto r = client.MonitorNode(5, 3'000'000); !r) { // alive while a heartbeat is under 3 s old
    return r.error;
  }
  const auto frame = client.Poll(100'000); // wait up to 100 ms and route one frame
  if (!frame) {
    return frame.error;
  }
  const auto status = client.GetNodeStatus(5);
  if (status && !status.value.alive) {
    return canopen_client::Error::Timeout; // no heartbeat for 3 s
  }
  canopen_client::EmcyEvent event{};
  while (client.PopEmcy(event)) {
    // event.node, event.code (0x0000 means the error is gone), event.error_register
  }
  return canopen_client::Error::Ok;
}
// --8<-- [end:monitor]

// --8<-- [start:pdo]
canopen_client::Error SetUpPositionPdo(Client& client) {
  canopen_client::PdoConfig pdo{};
  pdo.cob_id = 0x185;        // TPDO1 of node 5
  pdo.transmission_type = 1; // sent on every SYNC
  pdo.entry_count = 2;
  pdo.entries[0] = {0x6064, 0, 32}; // position actual value
  pdo.entries[1] = {0x6041, 0, 16}; // statusword
  if (auto r = client.ConfigurePdo(5, canopen_client::PdoKind::Transmit, 1, pdo); !r) {
    return r.error; // the PDO is left invalid, never half configured and valid
  }
  if (auto r = client.SubscribeTpdo(0x185, 6); !r) {
    return r.error;
  }
  if (auto r = client.SendNmt(canopen_client::NmtCommand::Start, 5); !r) {
    return r.error;
  }
  return client.SendSync().error; // the node answers with the PDO; `Poll()` then `ReadTpdo(0x185)`
}
// --8<-- [end:pdo]

// --8<-- [start:drive]
canopen_client::Error RunDrive(Client& client) {
  using canopen_client::OperationMode;
  if (auto r = client.SetDriveMode(5, OperationMode::ProfileVelocity); !r) {
    return r.error;
  }
  if (auto r = client.DriveEnable(5); !r) { // shutdown, switch on, enable operation
    return r.error; // DriveFault: the drive is in Fault; `DriveResetFault()` clears it
  }
  // ... write the target velocity (object 0x60FF) with `SdoWriteU32()` ...
  return client.DriveDisable(5).error; // never refused, sent up to Config::safety_attempts times
}
// --8<-- [end:drive]

// --8<-- [start:errors]
const char* Describe(canopen_client::Error e) {
  switch (e) {
  case canopen_client::Error::Timeout:
    return "no reply in time: node absent, wrong node id or bit rate";
  case canopen_client::Error::SdoAbort:
    return "the node refused the transfer: see GetLastAbort()";
  case canopen_client::Error::NodeState:
    return "the node is Stopped: send NmtCommand::Start or PreOperational first";
  default:
    return canopen_client::ToString(e);
  }
}
// --8<-- [end:errors]

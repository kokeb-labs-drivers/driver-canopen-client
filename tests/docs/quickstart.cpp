/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "canopen_client.hpp"

int read_node_and_start_it(MyPort& port, uint32_t& device_type) {
  canopen_client::Client<MyPort> client(port); // no I/O yet

  if (auto r = client.EnsureInitialized(); !r) { // 1. CAN controller up
    return static_cast<int>(r.error);            //    Bus: the port could not start it
  }
  const auto type = client.ReadDeviceType(5); //    2. SDO read of object 0x1000 from node 5
  if (!type) {
    return static_cast<int>(type.error); //    Timeout: nobody answered; SdoAbort: the node refused
  }
  device_type = type.value;
  if (auto r = client.SendNmt(canopen_client::NmtCommand::Start, 5);
      !r) { // 3. node 5 to Operational
    return static_cast<int>(r.error);
  }
  return 0;
}
// --8<-- [end:quickstart]

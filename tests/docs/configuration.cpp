/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "canopen_client.hpp"
#include "canopen_client_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:kconfig]
canopen_client::Error StartFromMenuconfig(MyPort& port) {
  static canopen_client::Client<MyPort> client(port, canopen_client::KconfigDefaults());
  return client.EnsureInitialized().error;
}
// --8<-- [end:kconfig]

// --8<-- [start:preset]
canopen_client::Error StartOnSlowBus(MyPort& port) {
  canopen_client::Config c = canopen_client::SlowBusPreset(); // 125 kbit/s, many nodes
  c.safety_attempts = 5;                                 // stop commands are sent up to 5 times
  static canopen_client::Client<MyPort> client(port, c); // stored, no I/O
  return client.EnsureInitialized().error; // InvalidArgument if a field is out of range
}
// --8<-- [end:preset]

/**
 * @file sdo_on_tx_only_port.cpp
 * @brief Must NOT compile: a transmit-only port cannot read a reply, so the calls that need one are
 * constrained away instead of failing at run time.
 *
 * EXPECT: CanRxPort
 */
#include <array>

#include "canopen_client.hpp"
#include "mock_canopen_client.hpp"

int main() {
  canopen_client_test::TxOnlyPort port;
  canopen_client::Client<canopen_client_test::TxOnlyPort> client(port);
  std::array<uint8_t, 4> buffer{};
  return static_cast<int>(client.SdoUpload(1, 0x1000, 0, buffer).error);
}

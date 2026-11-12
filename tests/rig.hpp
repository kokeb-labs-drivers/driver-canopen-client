/**
 * @file rig.hpp
 * @brief A started client on the simulated bus with nodes 5 and 6, shared by the client tests.
 */
#ifndef CANOPEN_CLIENT_RIG_HPP
#define CANOPEN_CLIENT_RIG_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "canopen_client.hpp"
#include "mock_canopen_client.hpp"
#include "test_harness.hpp"

namespace canopen_client_test {

using canopen_client::Error;

/// Two nodes (5 and 6, both pre-operational), a client over the mock port, initialised.
struct Rig {
  MockPort port;
  SimNode& n5;
  SimNode& n6;
  canopen_client::Client<MockPort> client;
  bool started;

  explicit Rig(const canopen_client::Config& config = {})
      : port(), n5(port.bus.AddNode(5)), n6(port.bus.AddNode(6)), client(port, config),
        started(client.EnsureInitialized().ok()) {}
  Rig(const Rig&) = delete;
  Rig& operator=(const Rig&) = delete;

  SimBus& Bus() {
    return port.bus;
  }
  [[nodiscard]] std::size_t Sent() const {
    return port.bus.sent.size();
  }
  /// Number of frames sent since `mark` that have identifier `id`.
  [[nodiscard]] std::size_t SentWithId(std::size_t mark, uint16_t id) const {
    std::size_t n = 0;
    for (std::size_t i = mark; i < port.bus.sent.size(); ++i) {
      if (port.bus.sent[i].id == id) {
        ++n;
      }
    }
    return n;
  }
};

} // namespace canopen_client_test

#endif // CANOPEN_CLIENT_RIG_HPP

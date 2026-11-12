/**
 * @file test_pdo.cpp
 * @brief PDO configuration over SDO in the profile's order, and PDO transfer, against the simulated
 * node.
 */
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using canopen_client::FrameClass;
using canopen_client::NmtCommand;
using canopen_client::PdoConfig;
using canopen_client::PdoKind;
using namespace canopen_client_test;

namespace {

/// The (index, sub, value) of every expedited SDO download the client sent to `node` since `mark`.
struct Write {
  uint16_t index;
  uint8_t sub;
  uint32_t value;
};

std::vector<Write> Writes(const Rig& r, std::size_t mark, uint8_t node) {
  std::vector<Write> out;
  for (std::size_t i = mark; i < r.port.bus.sent.size(); ++i) {
    const CanFrame& f = r.port.bus.sent[i];
    if (f.id == 0x600U + node && (f.data[0] >> 5) == 1) {
      const uint32_t v =
          static_cast<uint32_t>(f.data[4]) | (static_cast<uint32_t>(f.data[5]) << 8) |
          (static_cast<uint32_t>(f.data[6]) << 16) | (static_cast<uint32_t>(f.data[7]) << 24);
      out.push_back({static_cast<uint16_t>(static_cast<uint32_t>(f.data[1]) |
                                           (static_cast<uint32_t>(f.data[2]) << 8U)),
                     f.data[3], v});
    }
  }
  return out;
}

PdoConfig Tpdo() {
  PdoConfig c{};
  c.cob_id = 0x185;
  c.transmission_type = 1;
  c.entry_count = 2;
  c.entries[0] = {0x2001, 0, 16};
  c.entries[1] = {0x2002, 0, 8};
  return c;
}

} // namespace

TEST_CASE("A TPDO is configured in the profile's order and ends valid") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo()).ok());
  const auto w = Writes(r, mark, 5);
  REQUIRE(w.size() == 7);
  CHECK(w[0].index == 0x1800 && w[0].sub == 1 &&
        w[0].value == (0x185U | 0x80000000U));                      // invalidate
  CHECK(w[1].index == 0x1800 && w[1].sub == 2 && w[1].value == 1U); // type
  CHECK(w[2].index == 0x1A00 && w[2].sub == 0 && w[2].value == 0U); // clear count
  CHECK(w[3].index == 0x1A00 && w[3].sub == 1 && w[3].value == 0x20010010U);
  CHECK(w[4].index == 0x1A00 && w[4].sub == 2 && w[4].value == 0x20020008U);
  CHECK(w[5].index == 0x1A00 && w[5].sub == 0 && w[5].value == 2U);     // count
  CHECK(w[6].index == 0x1800 && w[6].sub == 1 && w[6].value == 0x185U); // valid
  CHECK_EQ(r.n5.Value(0x1800, 1), 0x185U);
  CHECK_EQ(r.n5.Value(0x1A00, 0), 2U);
}

TEST_CASE("Inhibit time and event timer are written only when set") {
  Rig r;
  REQUIRE(r.started);
  PdoConfig c = Tpdo();
  std::size_t mark = r.Sent();
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, c).ok());
  for (const Write& w : Writes(r, mark, 5)) {
    CHECK(w.index != 0x1800 || (w.sub != 3 && w.sub != 5));
  }
  c.inhibit_time_100us = 50;
  c.event_timer_ms = 200;
  mark = r.Sent();
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, c).ok());
  int timing_writes = 0;
  for (const Write& w : Writes(r, mark, 5)) {
    timing_writes += (w.index == 0x1800 && (w.sub == 3 || w.sub == 5)) ? 1 : 0;
  }
  CHECK_EQ(timing_writes, 2);
  CHECK_EQ(r.n5.Value(0x1800, 3), 50U);
  CHECK_EQ(r.n5.Value(0x1800, 5), 200U);
}

TEST_CASE("An RPDO has no inhibit time or timer, and the node consumes what is sent to it") {
  Rig r;
  REQUIRE(r.started);
  PdoConfig c{};
  c.cob_id = 0x205;
  c.transmission_type = 0xFF;
  c.inhibit_time_100us = 7; // not an RPDO parameter: never written
  c.event_timer_ms = 9;
  c.entry_count = 1;
  c.entries[0] = {0x2001, 0, 16};
  const std::size_t mark = r.Sent();
  CHECK(r.client.ConfigurePdo(5, PdoKind::Receive, 1, c).ok());
  CHECK_EQ(Writes(r, mark, 5).size(), 6U); // invalidate, type, clear, entry, count, valid
  CHECK(r.client.SendNmt(NmtCommand::Start, 5).ok());
  const std::array<uint8_t, 2> data{0x34, 0x12};
  CHECK(r.client.SendRpdo(0x205, data).ok());
  CHECK_EQ(r.n5.Value(0x2001, 0), 0x1234U);
  CHECK_EQ(r.n5.rpdo_log.size(), 1U);
}

TEST_CASE("A configured TPDO is produced on SYNC and arrives at a subscription") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo()).ok());
  CHECK(r.client.SubscribeTpdo(0x185, 3).ok());
  CHECK(r.client.SendNmt(NmtCommand::Start, 5).ok());
  CHECK(r.client.SendSync().ok());
  CHECK(r.client.Poll(5'000).value == FrameClass::Pdo);
  const auto s = r.client.ReadTpdo(0x185);
  REQUIRE(s.ok());
  CHECK(s.value.data[0] == 0xCD && s.value.data[1] == 0xAB && s.value.data[2] == 0x5A);
  // Pre-operational nodes do not produce PDOs.
  CHECK(r.client.SendNmt(NmtCommand::PreOperational, 5).ok());
  CHECK(r.client.SendSync().ok());
  CHECK(r.client.Poll(5'000).value == FrameClass::None);
}

TEST_CASE("Reconfiguring a valid PDO invalidates it first") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo()).ok());
  PdoConfig c{};
  c.cob_id = 0x195;
  c.transmission_type = 254;
  c.entry_count = 1;
  c.entries[0] = {0x2000, 0, 32};
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, c).ok());
  CHECK_EQ(r.n5.Value(0x1800, 1), 0x195U);
  CHECK_EQ(r.n5.Value(0x1A00, 0), 1U);
  CHECK_EQ(r.n5.Value(0x1A00, 1), 0x20000020U);
}

TEST_CASE("A PDO that is not enabled is left invalid") {
  Rig r;
  REQUIRE(r.started);
  PdoConfig c = Tpdo();
  c.enabled = false;
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, c).ok());
  CHECK((r.n5.Value(0x1800, 1) & 0x80000000U) != 0);
  CHECK_EQ(r.n5.Value(0x1A00, 0), 2U);
}

TEST_CASE("A failure part way leaves the PDO invalid, never half configured and valid") {
  Rig r;
  REQUIRE(r.started);
  PdoConfig c{};
  c.cob_id = 0x185;
  c.entry_count = 9; // the node has 8 mapping entries: the ninth write is refused
  for (std::size_t i = 0; i < 9; ++i) {
    c.entries[i] = {0x2002, 0, 1};
  }
  const auto e = r.client.ConfigurePdo(5, PdoKind::Transmit, 1, c);
  CHECK(!e.ok() && e.error == Error::SdoAbort);
  CHECK_EQ(r.client.GetLastAbort().code, 0x06090011U);
  CHECK((r.n5.Value(0x1800, 1) & 0x80000000U) != 0);
  // A node that goes silent half way: the first write that is not answered ends the call.
  r.n5.responds = false;
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 2, Tpdo()).error == Error::Timeout);
}

TEST_CASE("ConfigurePdo checks its arguments before sending anything") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  PdoConfig c = Tpdo();
  CHECK(r.client.ConfigurePdo(0, PdoKind::Transmit, 1, c).error == Error::InvalidArgument);
  CHECK(r.client.ConfigurePdo(128, PdoKind::Transmit, 1, c).error == Error::InvalidArgument);
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 0, c).error == Error::InvalidArgument);
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 513, c).error == Error::InvalidArgument);
  PdoConfig bad = c;
  bad.entry_count = 0;
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, bad).error == Error::InvalidArgument);
  bad = c;
  bad.cob_id = 0x7FF;
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, bad).error == Error::InvalidArgument);
  bad = c;
  bad.entries[0].bits = 0;
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, bad).error == Error::InvalidArgument);
  bad = c;
  bad.entry_count = 17;
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, bad).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
}

TEST_CASE("ReadPdoConfig returns what ConfigurePdo wrote, DisablePdo keeps the identifier") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.ConfigurePdo(5, PdoKind::Transmit, 1, Tpdo()).ok());
  const auto back = r.client.ReadPdoConfig(5, PdoKind::Transmit, 1);
  REQUIRE(back.ok());
  CHECK(back.value.cob_id == 0x185 && back.value.enabled && back.value.transmission_type == 1);
  CHECK(back.value.entry_count == 2);
  CHECK(back.value.entries[0].index == 0x2001 && back.value.entries[0].bits == 16);
  CHECK(back.value.entries[1].index == 0x2002 && back.value.entries[1].bits == 8);
  CHECK(r.client.DisablePdo(5, PdoKind::Transmit, 1).ok());
  CHECK_EQ(r.n5.Value(0x1800, 1), 0x185U | 0x80000000U);
  const auto off = r.client.ReadPdoConfig(5, PdoKind::Transmit, 1);
  CHECK(off.ok() && !off.value.enabled && off.value.cob_id == 0x185);
  CHECK(r.client.DisablePdo(5, PdoKind::Transmit, 1).ok()); // already invalid
}

TEST_CASE("ReadPdoConfig refuses a 29-bit identifier and an over-long mapping") {
  Rig r;
  REQUIRE(r.started);
  r.n5.Find(0x1800, 1)->value = Le(0x20000185U, 4);
  CHECK(r.client.ReadPdoConfig(5, PdoKind::Transmit, 1).error == Error::Unsupported);
  r.n5.Find(0x1800, 1)->value = Le(0x00000185U, 4);
  r.n5.Find(0x1A00, 0)->value = Le(17, 1);
  CHECK(r.client.ReadPdoConfig(5, PdoKind::Transmit, 1).error == Error::BufferTooSmall);
  CHECK(r.client.ReadPdoConfig(5, PdoKind::Transmit, 0).error == Error::InvalidArgument);
  CHECK(r.client.ReadPdoConfig(5, PdoKind::Transmit, 3).error == Error::SdoAbort); // no such record
  CHECK(r.client.DisablePdo(5, PdoKind::Receive, 600).error == Error::InvalidArgument);
}

TEST_CASE("SendRpdo checks the identifier and the length, PDO bytes can be assembled bit-exactly") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  const std::array<uint8_t, 3> d{1, 2, 3};
  CHECK(r.client.SendRpdo(0x180, d).error == Error::InvalidArgument);
  CHECK(r.client.SendRpdo(0x580, d).error == Error::InvalidArgument);
  CHECK(r.client.SendRpdo(0x205, std::array<uint8_t, 9>{}).error == Error::InvalidArgument);
  CHECK_EQ(r.Sent(), mark);
  CHECK(r.client.SendRpdo(0x205, std::span<const uint8_t>{}).ok()); // an empty PDO is legal
  CHECK(r.Bus().sent.back().dlc == 0);
  std::array<uint8_t, 4> buf{};
  CHECK(canopen_client::InsertBits(buf, 3, 12, 0xABC).ok());
  const auto v = canopen_client::ExtractBits(buf, 3, 12);
  CHECK(v.ok() && v.value == 0xABCU);
}

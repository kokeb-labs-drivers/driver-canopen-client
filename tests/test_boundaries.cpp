/**
 * @file test_boundaries.cpp
 * @brief Both sides of every numeric limit: the last accepted value and the first refused one.
 */
#include <array>
#include <cstdint>
#include <span>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using canopen_client::FrameClass;
using canopen_client::PdoConfig;
using canopen_client::PdoKind;
using namespace canopen_client_test;

namespace {

PdoConfig OneEntry(uint16_t cob_id) {
  PdoConfig c{};
  c.cob_id = cob_id;
  c.entry_count = 1;
  c.entries[0] = {0x2001, 0, 8};
  return c;
}

} // namespace

TEST_CASE("PDO identifier limits are inclusive") {
  const std::array<uint8_t, 1> d{1};
  CHECK(canopen_client::MakePdoFrame(canopen_client::kPdoCobIdMin, d).ok());
  CHECK(canopen_client::MakePdoFrame(canopen_client::kPdoCobIdMax, d).ok());
  CHECK(!canopen_client::MakePdoFrame(canopen_client::kPdoCobIdMin - 1, d).ok());
  CHECK(!canopen_client::MakePdoFrame(canopen_client::kPdoCobIdMax + 1, d).ok());
  const std::array<uint8_t, 9> nine{};
  CHECK(!canopen_client::MakePdoFrame(0x200, nine).ok());
  CHECK(canopen_client::MakePdoFrame(0x200, std::span<const uint8_t>(nine).first(8)).ok());
  CHECK(canopen_client::ValidatePdoConfig(OneEntry(canopen_client::kPdoCobIdMin)) == Error::Ok);
  CHECK(canopen_client::ValidatePdoConfig(OneEntry(canopen_client::kPdoCobIdMax)) == Error::Ok);
  CHECK(canopen_client::ValidatePdoConfig(OneEntry(canopen_client::kPdoCobIdMin - 1)) ==
        Error::InvalidArgument);
  CHECK(canopen_client::ValidatePdoConfig(OneEntry(canopen_client::kPdoCobIdMax + 1)) ==
        Error::InvalidArgument);
}

TEST_CASE("PDO entry limits are inclusive") {
  PdoConfig c = OneEntry(0x185);
  c.entries[0].bits = 64;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  c.entries[0].bits = 65;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::InvalidArgument);
  c.entries[0].bits = 1;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  c.entries[0].bits = 0;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::InvalidArgument);
  c = OneEntry(0x185);
  c.entries[0].index = 1;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  c.entries[0].index = 0;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::InvalidArgument);
  // 16 one-bit entries fit; the count limit itself is inclusive, one more is refused.
  c = OneEntry(0x185);
  c.entry_count = canopen_client::kMaxPdoEntries;
  for (std::size_t i = 0; i < canopen_client::kMaxPdoEntries; ++i) {
    c.entries[i] = {static_cast<uint16_t>(0x2000 + i), 0, 1};
  }
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  c.entry_count = canopen_client::kMaxPdoEntries + 1;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::InvalidArgument);
  // An entry past entry_count is not looked at, however wrong it is.
  c = OneEntry(0x185);
  c.entries[1] = {0, 0, 0};
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  CHECK_EQ(canopen_client::PdoBitLength(c), 8U);
  // 64 bits in all fit, 65 do not.
  c.entry_count = 2;
  c.entries[0] = {0x2001, 0, 32};
  c.entries[1] = {0x2002, 0, 32};
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::Ok);
  c.entries[1].bits = 33;
  CHECK(canopen_client::ValidatePdoConfig(c) == Error::InvalidArgument);
}

TEST_CASE("Period and timeout limits are inclusive") {
  Rig r;
  REQUIRE(r.started);
  const uint32_t max = canopen_client::kMaxSdoTimeoutUs;
  CHECK(r.client.ConfigureSync(0, 0).ok());
  CHECK(r.client.ConfigureSync(100, 0).ok());
  CHECK(r.client.ConfigureSync(max, 0).ok());
  CHECK(r.client.ConfigureSync(max + 1, 0).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(99, 0).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(1'000, 2).ok());
  CHECK(r.client.ConfigureSync(1'000, 240).ok());
  CHECK(r.client.ConfigureSync(1'000, 1).error == Error::InvalidArgument);
  CHECK(r.client.ConfigureSync(1'000, 241).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(5, 1'000).ok());
  CHECK(r.client.MonitorNode(5, max).ok());
  CHECK(r.client.MonitorNode(5, max + 1).error == Error::InvalidArgument);
  CHECK(r.client.MonitorNode(5, 999).error == Error::InvalidArgument);
}

TEST_CASE("A node counts as alive up to and including its timeout") {
  Rig r;
  REQUIRE(r.started);
  REQUIRE(r.client.MonitorNode(5, 100'000).ok());
  r.Bus().Inject(Frame(0x705, {0x05}), r.Bus().now_us + 10);
  REQUIRE(r.client.Poll(1'000).value == FrameClass::Heartbeat);
  const uint64_t seen = r.Bus().now_us;
  r.Bus().now_us = seen + 100'000;
  CHECK(r.client.GetNodeStatus(5).value.alive);
  r.Bus().now_us = seen + 100'001;
  CHECK(!r.client.GetNodeStatus(5).value.alive);
}

TEST_CASE("Identifiers at the edge of a range belong to no node") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().Inject(Frame(0x580, {0x00}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  CHECK_EQ(r.client.GetStats().sdo_stale, 0U); // 0x580 is not a reply from node 0
  r.Bus().Inject(Frame(0x581, {0x00}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored);
  CHECK_EQ(r.client.GetStats().sdo_stale, 1U);
  r.Bus().Inject(Frame(0x700, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Ignored); // not a heartbeat of node 0
  r.Bus().Inject(Frame(0x7FF, {0x05}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value != FrameClass::Heartbeat);
  REQUIRE(r.client.MonitorNode(127, 100'000).ok());
  r.Bus().Inject(Frame(0x77F, {0x7F}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).value == FrameClass::Heartbeat); // node 127 is the last
}

TEST_CASE("SendRpdo accepts the range of PDO identifiers and no more") {
  Rig r;
  REQUIRE(r.started);
  const std::array<uint8_t, 2> d{1, 2};
  CHECK(r.client.SendRpdo(0x57F, d).ok());
  CHECK(r.client.SendRpdo(0x580, d).error == Error::InvalidArgument);
  CHECK(r.client.SendRpdo(0x181, d).ok());
  CHECK(r.client.SendRpdo(0x180, d).error == Error::InvalidArgument);
}

TEST_CASE("ReadPdoConfig fails when only the type or only the count fails") {
  for (int which = 0; which < 2; ++which) {
    Rig r;
    REQUIRE(r.started);
    Object* o = which == 0 ? r.n5.Find(0x1800, 2) : r.n5.Find(0x1A00, 0);
    REQUIRE(o != nullptr);
    o->readable = false;
    CHECK(!r.client.ReadPdoConfig(5, PdoKind::Transmit, 1).ok());
  }
}

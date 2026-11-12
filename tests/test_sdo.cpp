/**
 * @file test_sdo.cpp
 * @brief The SDO client against the simulated node: transfers, aborts, timeouts, stale replies.
 */
#include <array>
#include <cstdint>

#include "rig.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using namespace canopen_client_test;

TEST_CASE("Expedited reads return the stored value at the right width") {
  Rig r;
  REQUIRE(r.started);
  const auto d = r.client.SdoReadU32(5, 0x2000, 0);
  CHECK(d.ok() && d.value == 0x11223344U);
  const auto w = r.client.SdoReadU16(5, 0x2001, 0);
  CHECK(w.ok() && w.value == 0xABCDU);
  const auto b = r.client.SdoReadU8(5, 0x2002, 0);
  CHECK(b.ok() && b.value == 0x5AU);
  const auto t = r.client.ReadDeviceType(5);
  CHECK(t.ok() && t.value == 0x00020192U);
  CHECK(r.client.ReadErrorRegister(5).ok());
}

TEST_CASE("A read of the wrong width is refused and the node is told") {
  Rig r;
  REQUIRE(r.started);
  const auto bad = r.client.SdoReadU16(5, 0x2000, 0); // 4-byte object
  CHECK(!bad.ok() && bad.error == Error::Protocol);
  CHECK(r.n5.aborts_received ==
        0); // an expedited reply ends the transfer, there is nothing to abort
  const auto bad8 = r.client.SdoReadU8(5, 0x2001, 0); // 2-byte object
  CHECK(!bad8.ok() && bad8.error == Error::Protocol);
  const auto bad32 = r.client.SdoReadU32(5, 0x2002, 0); // 1-byte object
  CHECK(!bad32.ok() && bad32.error == Error::Protocol);
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).ok()); // the client recovered
}

TEST_CASE("Expedited writes change the object and are acknowledged") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.SdoWriteU32(5, 0x2000, 0, 0xA1B2C3D4U).ok());
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).value == 0xA1B2C3D4U);
  CHECK(r.client.SdoWriteU16(5, 0x2001, 0, 0x1234).ok());
  CHECK(r.client.SdoReadU16(5, 0x2001, 0).value == 0x1234U);
  CHECK(r.client.SdoWriteU8(5, 0x2002, 0, 0x77).ok());
  CHECK(r.client.SdoReadU8(5, 0x2002, 0).value == 0x77U);
  CHECK(r.client.SdoWriteU16(6, 0x2001, 0, 0x4321).ok());
  CHECK(r.client.SdoReadU16(5, 0x2001, 0).value == 0x1234U); // the other node is untouched
}

TEST_CASE("Aborts from the node surface with the code, index and sub-index") {
  Rig r;
  REQUIRE(r.started);
  auto a = r.client.SdoReadU32(5, 0x3000, 0);
  CHECK(!a.ok() && a.error == Error::SdoAbort);
  CHECK(r.client.GetLastAbort().code == 0x06020000U && r.client.GetLastAbort().remote);
  CHECK(r.client.GetLastAbort().index == 0x3000 && r.client.GetLastAbort().node == 5);
  a = r.client.SdoReadU32(5, 0x2000, 3);
  CHECK(!a.ok() && r.client.GetLastAbort().code == 0x06090011U && r.client.GetLastAbort().sub == 3);
  const auto w = r.client.SdoWriteU32(5, 0x2003, 0, 1); // read-only
  CHECK(!w.ok() && r.client.GetLastAbort().code == 0x06010002U);
  const auto rd = r.client.SdoReadU16(5, 0x2004, 0); // write-only
  CHECK(!rd.ok() && r.client.GetLastAbort().code == 0x06010001U);
  r.n5.abort_next_with = 0x08000000U;
  const auto g = r.client.SdoReadU32(5, 0x2000, 0);
  CHECK(!g.ok() && g.error == Error::SdoAbort && r.client.GetLastAbort().code == 0x08000000U);
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).ok()); // and the next request is fine
  CHECK(r.n5.aborts_received == 0);              // the client does not answer an abort with one
}

TEST_CASE("A node that never answers times out after the SDO timeout and sends an abort") {
  canopen_client::Config c;
  c.sdo_timeout_us = 5'000;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.responds = false;
  const uint64_t t0 = r.Bus().now_us;
  const auto v = r.client.SdoReadU32(5, 0x2000, 0);
  CHECK(!v.ok() && v.error == Error::Timeout);
  const uint64_t waited = r.Bus().now_us - t0;
  const uint64_t lo = 5000U;
  const uint64_t hi = 6000U;
  CHECK((waited >= lo) && (waited <= hi));
  CHECK(r.client.GetLastAbort().code == 0x05040000U && !r.client.GetLastAbort().remote);
  // The node is dead in this model, so look at what the client put on the bus.
  const CanFrame& last = r.Bus().sent.back();
  CHECK((last.id == 0x605) && (last.data[0] == 0x80) && (last.data[1] == 0x00) &&
        (last.data[2] == 0x20));
  CHECK((last.data[4] == 0x00) && (last.data[5] == 0x00) && (last.data[6] == 0x04) &&
        (last.data[7] == 0x05));
}

TEST_CASE("With abort_on_timeout off the timeout sends nothing more") {
  canopen_client::Config c;
  c.sdo_timeout_us = 5'000;
  c.abort_on_timeout = false;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.responds = false;
  const std::size_t mark = r.Sent();
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).error == Error::Timeout);
  CHECK(r.Sent() == mark + 1);
}

TEST_CASE("Segmented upload of a domain object, 12 bytes and exactly 7 and 14") {
  Rig r;
  REQUIRE(r.started);
  std::array<uint8_t, 64> buf{};
  const auto n = r.client.SdoUpload(5, 0x2100, 0, buf);
  REQUIRE(n.ok());
  CHECK(n.value == 12U);
  CHECK(buf[0] == 'C' && buf[11] == 'e');
  Object* o = r.n5.Find(0x2100, 0);
  REQUIRE(o != nullptr);
  o->value.assign(7, 0x41);
  CHECK(r.client.SdoUpload(5, 0x2100, 0, buf).value == 7U);
  o->value.assign(14, 0x42);
  const auto n14 = r.client.SdoUpload(5, 0x2100, 0, buf);
  CHECK(n14.ok() && n14.value == 14U && buf[13] == 0x42);
  o->value.assign(8, 0x43);
  CHECK(r.client.SdoUpload(5, 0x2100, 0, buf).value == 8U);
}

TEST_CASE("Upload into a buffer that is too small aborts and says so") {
  Rig r;
  REQUIRE(r.started);
  std::array<uint8_t, 5> small{};
  const auto n = r.client.SdoUpload(5, 0x2100, 0, small);
  CHECK(!n.ok() && n.error == Error::BufferTooSmall);
  CHECK(r.n5.aborts_received == 1);
  std::array<uint8_t, 2> tiny{};
  const auto e = r.client.SdoUpload(5, 0x2000, 0, tiny); // expedited 4 bytes into 2
  CHECK(!e.ok() && e.error == Error::BufferTooSmall);
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).ok());
}

TEST_CASE("Segmented download writes a domain object of any length up to its capacity") {
  Rig r;
  REQUIRE(r.started);
  std::array<uint8_t, 40> data{};
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(i + 1U);
  }
  CHECK(r.client.SdoDownload(5, 0x2100, 0, data).ok());
  std::array<uint8_t, 64> back{};
  const auto n = r.client.SdoUpload(5, 0x2100, 0, back);
  CHECK(n.ok() && n.value == 40U && back[0] == 1 && back[39] == 40);
  for (const std::size_t len :
       {std::size_t{5}, std::size_t{7}, std::size_t{8}, std::size_t{14}, std::size_t{15}}) {
    CHECK(r.client.SdoDownload(5, 0x2100, 0, std::span<const uint8_t>(data).first(len)).ok());
    const auto m = r.client.SdoUpload(5, 0x2100, 0, back);
    CHECK(m.ok() && m.value == len);
  }
  std::array<uint8_t, 65> too_long{};
  const auto bad = r.client.SdoDownload(5, 0x2100, 0, too_long);
  CHECK(!bad.ok() && bad.error == Error::SdoAbort);
  CHECK(r.client.GetLastAbort().code == 0x06070012U);
}

TEST_CASE("Download of 1 to 4 bytes is expedited, other lengths to a variable object are refused "
          "by the node") {
  Rig r;
  REQUIRE(r.started);
  const std::array<uint8_t, 4> four{1, 2, 3, 4};
  CHECK(r.client.SdoDownload(5, 0x2000, 0, four).ok());
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).value == 0x04030201U);
  const auto two = r.client.SdoDownload(5, 0x2000, 0, std::span<const uint8_t>(four).first(2));
  CHECK(!two.ok() && r.client.GetLastAbort().code == 0x06070013U);
  const std::array<uint8_t, 5> five{};
  const auto f = r.client.SdoDownload(5, 0x2000, 0, five);
  CHECK(!f.ok() && f.error == Error::SdoAbort && r.client.GetLastAbort().code == 0x06070012U);
}

TEST_CASE("A node without segmented transfer aborts a segmented request and the client reports the "
          "abort") {
  Rig r;
  REQUIRE(r.started);
  r.n5.supports_segmented = false;
  std::array<uint8_t, 64> buf{};
  const auto n = r.client.SdoUpload(5, 0x2100, 0, buf);
  CHECK(!n.ok() && n.error == Error::SdoAbort);
}

TEST_CASE("Arguments are checked before anything is sent") {
  Rig r;
  REQUIRE(r.started);
  const std::size_t mark = r.Sent();
  std::array<uint8_t, 4> buf{};
  const std::array<uint8_t, 0> none{};
  CHECK(r.client.SdoReadU32(0, 0x2000, 0).error == Error::InvalidArgument);
  CHECK(r.client.SdoReadU32(128, 0x2000, 0).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownload(5, 0x2000, 0, none).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownload(0, 0x2000, 0, buf).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownloadUnconfirmed(5, 0x2000, 0, none).error == Error::InvalidArgument);
  CHECK(r.client.SdoDownloadUnconfirmed(5, 0x2000, 0, std::array<uint8_t, 5>{}).error ==
        Error::InvalidArgument);
  CHECK(r.client.SdoDownloadUnconfirmed(128, 0x2000, 0, buf).error == Error::InvalidArgument);
  CHECK(r.Sent() == mark);
}

TEST_CASE("The unconfirmed write sends one frame and waits for nothing") {
  Rig r;
  REQUIRE(r.started);
  const uint64_t t0 = r.Bus().now_us;
  const std::array<uint8_t, 2> v{0x34, 0x12};
  CHECK(r.client.SdoDownloadUnconfirmed(5, 0x2001, 0, v).ok());
  CHECK(r.Bus().now_us == t0);
  CHECK(r.Bus().sent.back().id == 0x605 && r.Bus().sent.back().data[0] == 0x2B);
}

TEST_CASE("A stale reply to an earlier request does not satisfy the next one") {
  Rig r;
  REQUIRE(r.started);
  // The node's answer to a request arrives after the client has given up on it.
  r.n5.responds = false;
  canopen_client::Config c = r.client.GetConfig();
  CHECK(r.client.SetConfig(c).ok());
  static_cast<void>(r.client.SdoReadU16(5, 0x2001, 0));
  r.n5.responds = true;
  // A late answer for 0x2001 is on the bus when the client asks for 0x2000.
  r.Bus().Inject(Frame(0x585, {0x4B, 0x01, 0x20, 0x00, 0xCD, 0xAB, 0, 0}), r.Bus().now_us + 10);
  const auto v = r.client.SdoReadU32(5, 0x2000, 0);
  CHECK(v.ok() && v.value == 0x11223344U);
  CHECK(r.client.GetStats().sdo_stale >= 1U);
}

TEST_CASE("Replies with the wrong node, index, kind, toggle or length never complete a request") {
  struct Case {
    const char* name;
    std::vector<uint8_t> bytes;
    uint16_t id;
    Error expect;
  };
  const Case cases[] = {
      {"other node", {0x43, 0x00, 0x20, 0x00, 1, 2, 3, 4}, 0x586, Error::Timeout},
      {"request id", {0x43, 0x00, 0x20, 0x00, 1, 2, 3, 4}, 0x605, Error::Timeout},
      {"other index", {0x43, 0x01, 0x20, 0x00, 1, 2, 3, 4}, 0x585, Error::Timeout},
      {"other sub", {0x43, 0x00, 0x20, 0x01, 1, 2, 3, 4}, 0x585, Error::Timeout},
      {"download ack for an upload", {0x60, 0x00, 0x20, 0x00, 0, 0, 0, 0}, 0x585, Error::Protocol},
      {"segment for an initiate", {0x00, 1, 2, 3, 4, 5, 6, 7}, 0x585, Error::Protocol},
      {"short", {0x43, 0x00, 0x20, 0x00, 1, 2, 3}, 0x585, Error::Protocol},
      {"reserved cs", {0xE0, 0x00, 0x20, 0x00, 1, 2, 3, 4}, 0x585, Error::Protocol},
      {"abort for another index", {0x80, 0x01, 0x20, 0x00, 0, 0, 2, 6}, 0x585, Error::Timeout},
  };
  for (const Case& cs : cases) {
    canopen_client::Config c;
    c.sdo_timeout_us = 4'000;
    Rig r(c);
    REQUIRE(r.started);
    r.n5.responds = false;
    r.Bus().Inject(Frame(cs.id, cs.bytes), r.Bus().now_us + 50);
    const auto v = r.client.SdoReadU32(5, 0x2000, 0);
    if (v.ok() || (v.error != cs.expect)) {
      ut::ReportFailure(__FILE__, __LINE__, cs.name);
    }
  }
}

TEST_CASE("A download acknowledgement for another object is not accepted either") {
  canopen_client::Config c;
  c.sdo_timeout_us = 4'000;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.responds = false;
  r.Bus().Inject(Frame(0x585, {0x60, 0x01, 0x20, 0x00, 0, 0, 0, 0}), r.Bus().now_us + 50);
  const auto w = r.client.SdoWriteU32(5, 0x2000, 0, 5);
  CHECK(!w.ok() && w.error == Error::Timeout);
}

TEST_CASE("The toggle bit is checked in segmented transfers") {
  Rig r;
  REQUIRE(r.started);
  Object* o = r.n5.Find(0x2100, 0);
  REQUIRE(o != nullptr);
  o->value.assign(30, 0x55);
  int seen = 0;
  r.Bus().mutate = [&seen](CanFrame& f, bool&) {
    if ((f.id == 0x585) && ((f.data[0] >> 5) == 0)) {
      ++seen;
      if (seen == 2) {
        f.data[0] = static_cast<uint8_t>(f.data[0] ^ 0x10U); // second segment with the wrong toggle
      }
    }
  };
  std::array<uint8_t, 64> buf{};
  const auto n = r.client.SdoUpload(5, 0x2100, 0, buf);
  CHECK(!n.ok() && n.error == Error::Protocol);
  CHECK(r.n5.aborts_received == 1 && r.n5.last_abort_code == 0x05030000U);
  r.Bus().mutate = nullptr;
  CHECK(r.client.SdoUpload(5, 0x2100, 0, buf).ok());
}

TEST_CASE("A duplicated reply does not complete the next request") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().duplicate_after_us = 100;
  const auto a = r.client.SdoReadU16(5, 0x2001, 0);
  CHECK(a.ok() && a.value == 0xABCDU);
  r.Bus().duplicate_after_us = 0;
  const auto b = r.client.SdoReadU8(5, 0x2002, 0);
  CHECK(b.ok() && b.value == 0x5AU);
}

TEST_CASE("The lenient length setting accepts a short reply the strict one refuses") {
  const auto shorten = [](CanFrame& f, bool&) {
    if (f.id == 0x585) {
      f.dlc = 6; // a 2-byte expedited upload needs 4 + 2 bytes
    }
  };
  Rig s;
  REQUIRE(s.started);
  s.Bus().mutate = shorten;
  CHECK(!s.client.SdoReadU16(5, 0x2001, 0).ok());
  canopen_client::Config lenient;
  lenient.strict_sdo_length = false;
  Rig l(lenient);
  REQUIRE(l.started);
  l.Bus().mutate = shorten;
  const auto v = l.client.SdoReadU16(5, 0x2001, 0);
  CHECK(v.ok() && v.value == 0xABCDU);
  // Lenient is still not blind: a reply that lacks announced data is refused.
  l.Bus().mutate = [](CanFrame& f, bool&) {
    if (f.id == 0x585) {
      f.dlc = 5;
    }
  };
  CHECK(!l.client.SdoReadU16(5, 0x2001, 0).ok());
}

TEST_CASE(
    "A bus error while waiting is reported, a failed send is reported and clears the request") {
  Rig r;
  REQUIRE(r.started);
  r.Bus().receive_error = true;
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).error == Error::Bus);
  r.Bus().receive_error = false;
  r.Bus().send_ok = false;
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).error == Error::Bus);
  r.Bus().send_ok = true;
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).ok());
}

TEST_CASE("A send that fails half way through a segmented transfer ends it") {
  Rig r;
  REQUIRE(r.started);
  std::array<uint8_t, 30> data{};
  r.Bus().fail_send_index = r.Bus().sends + 2;
  CHECK(r.client.SdoDownload(5, 0x2100, 0, data).error == Error::Bus);
  r.Bus().fail_send_index = -1;
  CHECK(r.client.SdoDownload(5, 0x2100, 0, data).ok());
}

TEST_CASE("A flood of unrelated frames cannot hold a request past its timeout") {
  canopen_client::Config c;
  c.sdo_timeout_us = 20'000;
  Rig r(c);
  REQUIRE(r.started);
  r.n5.responds = false;
  for (uint64_t i = 0; i < 2000; ++i) {
    r.Bus().Inject(Frame(0x700 + 6, {0x05}), r.Bus().now_us + i);
  }
  const auto v = r.client.SdoReadU32(5, 0x2000, 0);
  CHECK(!v.ok() && (v.error == Error::Timeout));
  CHECK(r.client.GetStats().flood_aborts <= 1U);
}

TEST_CASE("Store and restore send the signatures and refuse bad groups") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.StoreParameters(5, 1).ok());
  CHECK(r.client.RestoreDefaults(5, 1).ok());
  const std::size_t mark = r.Sent();
  CHECK(r.client.StoreParameters(5, 0).error == Error::InvalidArgument);
  CHECK(r.client.StoreParameters(5, 5).error == Error::InvalidArgument);
  CHECK(r.client.RestoreDefaults(5, 0).error == Error::InvalidArgument);
  CHECK(r.Sent() == mark);
  const auto missing = r.client.StoreParameters(5, 4); // the node has no such sub-index
  CHECK(!missing.ok() && missing.error == Error::SdoAbort);
}

TEST_CASE("Heartbeat producer time is written as 16 bits") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.WriteHeartbeatProducerTime(5, 250).ok());
  CHECK(r.client.SdoReadU16(5, 0x1017, 0).value == 250U);
  CHECK(r.client.WriteHeartbeatProducerTime(0, 250).error == Error::InvalidArgument);
}

TEST_CASE("An SDO to a node that has said it is stopped is refused without sending") {
  Rig r;
  REQUIRE(r.started);
  CHECK(r.client.MonitorNode(5, 100'000).ok());
  r.Bus().Inject(Frame(0x705, {0x04}), r.Bus().now_us + 10);
  CHECK(r.client.Poll(1'000).ok());
  const std::size_t mark = r.Sent();
  const auto v = r.client.SdoReadU32(5, 0x2000, 0);
  CHECK(!v.ok() && v.error == Error::NodeState);
  CHECK(r.Sent() == mark);
  // Another node is not affected, and a start command lifts the block at once.
  CHECK(r.client.SdoReadU32(6, 0x2000, 0).ok());
  CHECK(r.client.SendNmt(canopen_client::NmtCommand::Start, 5).ok());
  CHECK(r.client.SdoReadU32(5, 0x2000, 0).ok());
}

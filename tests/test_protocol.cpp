/**
 * @file test_protocol.cpp
 * @brief The frame codecs against byte vectors written out by hand from the protocol layouts.
 */
#include <array>
#include <cstdint>

#include "canopen_client.hpp"
#include "mock_canopen_client.hpp"
#include "test_harness.hpp"

using canopen_client::CanFrame;
using canopen_client::Error;
using canopen_client_test::Frame;

namespace {

bool Same(const CanFrame& f, uint16_t id, std::initializer_list<uint8_t> bytes) {
  if ((f.id != id) || (f.dlc != bytes.size())) {
    return false;
  }
  std::size_t i = 0;
  for (const uint8_t b : bytes) {
    if (f.data[i++] != b) {
      return false;
    }
  }
  return !f.rtr;
}

} // namespace

TEST_CASE("Identifiers follow the pre-defined connection set") {
  CHECK_EQ(canopen_client::SdoRequestId(5), 0x605);
  CHECK_EQ(canopen_client::SdoResponseId(5), 0x585);
  CHECK_EQ(canopen_client::HeartbeatId(127), 0x77F);
  CHECK_EQ(canopen_client::EmcyId(1), 0x081);
  CHECK(canopen_client::IsNodeId(1) && canopen_client::IsNodeId(127));
  CHECK(!canopen_client::IsNodeId(0) && !canopen_client::IsNodeId(128));
}

TEST_CASE("NMT frames: identifier 0, command then node") {
  using canopen_client::NmtCommand;
  CHECK(Same(canopen_client::MakeNmtFrame(NmtCommand::Start, 5).value, 0x000, {0x01, 0x05}));
  CHECK(Same(canopen_client::MakeNmtFrame(NmtCommand::Stop, 127).value, 0x000, {0x02, 0x7F}));
  CHECK(
      Same(canopen_client::MakeNmtFrame(NmtCommand::PreOperational, 0).value, 0x000, {0x80, 0x00}));
  CHECK(Same(canopen_client::MakeNmtFrame(NmtCommand::ResetNode, 1).value, 0x000, {0x81, 0x01}));
  CHECK(Same(canopen_client::MakeNmtFrame(NmtCommand::ResetCommunication, 2).value, 0x000,
             {0x82, 0x02}));
  CHECK(canopen_client::MakeNmtFrame(NmtCommand::Start, 128).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeNmtFrame(ut::RawEnum<NmtCommand>(0x03), 1).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeNmtFrame(ut::RawEnum<NmtCommand>(0x00), 1).error ==
        Error::InvalidArgument);
}

TEST_CASE("SYNC and guarding request frames") {
  CHECK(Same(canopen_client::MakeSyncFrame(0).value, 0x080, {}));
  CHECK(Same(canopen_client::MakeSyncFrame(7).value, 0x080, {0x07}));
  CHECK(Same(canopen_client::MakeSyncFrame(240).value, 0x080, {0xF0}));
  CHECK(canopen_client::MakeSyncFrame(241).error == Error::InvalidArgument);
  const auto g = canopen_client::MakeGuardRequest(9);
  CHECK(g.ok() && g.value.rtr && g.value.id == 0x709 && g.value.dlc == 0);
  CHECK(canopen_client::MakeGuardRequest(0).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeGuardRequest(128).error == Error::InvalidArgument);
}

TEST_CASE("SDO request frames are laid out as the protocol says") {
  const std::array<uint8_t, 4> v4{0x78, 0x56, 0x34, 0x12};
  CHECK(Same(canopen_client::MakeSdoUploadRequest(5, 0x6064, 2).value, 0x605,
             {0x40, 0x64, 0x60, 0x02, 0, 0, 0, 0}));
  CHECK(Same(canopen_client::MakeSdoExpeditedDownload(5, 0x2000, 0, v4).value, 0x605,
             {0x23, 0x00, 0x20, 0x00, 0x78, 0x56, 0x34, 0x12}));
  CHECK(Same(
      canopen_client::MakeSdoExpeditedDownload(5, 0x2000, 0, std::span<const uint8_t>(v4).first(2))
          .value,
      0x605, {0x2B, 0x00, 0x20, 0x00, 0x78, 0x56, 0, 0}));
  CHECK(Same(
      canopen_client::MakeSdoExpeditedDownload(5, 0x2000, 0, std::span<const uint8_t>(v4).first(1))
          .value,
      0x605, {0x2F, 0x00, 0x20, 0x00, 0x78, 0, 0, 0}));
  CHECK(Same(
      canopen_client::MakeSdoExpeditedDownload(5, 0x2000, 0, std::span<const uint8_t>(v4).first(3))
          .value,
      0x605, {0x27, 0x00, 0x20, 0x00, 0x78, 0x56, 0x34, 0}));
  CHECK(Same(canopen_client::MakeSdoSegmentedDownload(5, 0x2100, 1, 0x00000102).value, 0x605,
             {0x21, 0x00, 0x21, 0x01, 0x02, 0x01, 0x00, 0x00}));
  CHECK(Same(canopen_client::MakeSdoUploadSegmentRequest(5, false).value, 0x605,
             {0x60, 0, 0, 0, 0, 0, 0, 0}));
  CHECK(Same(canopen_client::MakeSdoUploadSegmentRequest(5, true).value, 0x605,
             {0x70, 0, 0, 0, 0, 0, 0, 0}));
  CHECK(Same(canopen_client::MakeSdoAbort(5, 0x1234, 7, 0x05040000).value, 0x605,
             {0x80, 0x34, 0x12, 0x07, 0x00, 0x00, 0x04, 0x05}));
}

TEST_CASE("SDO download segments carry toggle, unused-byte count and the last flag") {
  const std::array<uint8_t, 7> seven{1, 2, 3, 4, 5, 6, 7};
  const std::span<const uint8_t> all(seven);
  CHECK(Same(canopen_client::MakeSdoDownloadSegment(5, false, all, false).value, 0x605,
             {0x00, 1, 2, 3, 4, 5, 6, 7}));
  CHECK(Same(canopen_client::MakeSdoDownloadSegment(5, true, all, true).value, 0x605,
             {0x11, 1, 2, 3, 4, 5, 6, 7}));
  CHECK(Same(canopen_client::MakeSdoDownloadSegment(5, false, all.first(3), true).value, 0x605,
             {0x09, 1, 2, 3, 0, 0, 0, 0}));
  CHECK(Same(canopen_client::MakeSdoDownloadSegment(5, true, all.first(1), false).value, 0x605,
             {0x1C, 1, 0, 0, 0, 0, 0, 0}));
}

TEST_CASE("SDO builders refuse bad arguments") {
  const std::array<uint8_t, 8> eight{};
  const std::span<const uint8_t> all(eight);
  CHECK(canopen_client::MakeSdoUploadRequest(0, 0x1000, 0).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoUploadRequest(128, 0x1000, 0).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoExpeditedDownload(5, 0x2000, 0, all.first(0)).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoExpeditedDownload(5, 0x2000, 0, all.first(5)).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoExpeditedDownload(0, 0x2000, 0, all.first(2)).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoSegmentedDownload(5, 0x2000, 0, 0).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoSegmentedDownload(0, 0x2000, 0, 8).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoDownloadSegment(5, false, all.first(0), true).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoDownloadSegment(5, false, all.first(8), true).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoDownloadSegment(0, false, all.first(1), true).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoUploadSegmentRequest(0, false).error == Error::InvalidArgument);
  CHECK(canopen_client::MakeSdoAbort(0, 1, 1, 1).error == Error::InvalidArgument);
}

TEST_CASE("SDO replies decode: expedited upload with and without a size") {
  using canopen_client::SdoReplyKind;
  auto r = canopen_client::ParseSdoReply(
      Frame(0x585, {0x43, 0x00, 0x10, 0x00, 0x92, 0x01, 0x02, 0x00}), true);
  CHECK(r.ok() && r.value.kind == SdoReplyKind::InitiateUpload && r.value.expedited &&
        r.value.size_indicated);
  CHECK(r.value.index == 0x1000 && r.value.sub == 0 && r.value.data_len == 4 &&
        r.value.data[0] == 0x92);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x4B, 0x41, 0x60, 0x00, 0x37, 0x02, 0, 0}), true);
  CHECK(r.ok() && r.value.data_len == 2 && r.value.index == 0x6041 && r.value.data[1] == 0x02);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x4F, 0x02, 0x20, 0x00, 0x5A, 0, 0, 0}), true);
  CHECK(r.ok() && r.value.data_len == 1 && r.value.data[0] == 0x5A);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x42, 0x02, 0x20, 0x00, 0x5A, 1, 2, 3}), true);
  CHECK(r.ok() && r.value.expedited && !r.value.size_indicated && r.value.data_len == 4);
}

TEST_CASE("SDO replies decode: segmented upload, segments, download responses and aborts") {
  using canopen_client::SdoReplyKind;
  auto r =
      canopen_client::ParseSdoReply(Frame(0x585, {0x41, 0x08, 0x10, 0x00, 0x0C, 0, 0, 0}), true);
  CHECK(r.ok() && !r.value.expedited && r.value.size_indicated && r.value.size == 12);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x40, 0x08, 0x10, 0x00, 0, 0, 0, 0}), true);
  CHECK(r.ok() && !r.value.size_indicated && r.value.size == 0);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x10, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}), true);
  CHECK(r.ok() && r.value.kind == SdoReplyKind::SegmentUpload && r.value.toggle && !r.value.last &&
        r.value.data_len == 7 && r.value.data[6] == 'g');
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x09, 'x', 'y', 'z', 0, 0, 0, 0}), true);
  CHECK(r.ok() && !r.value.toggle && r.value.last && r.value.data_len == 3);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x60, 0x00, 0x20, 0x01, 0, 0, 0, 0}), true);
  CHECK(r.ok() && r.value.kind == SdoReplyKind::InitiateDownload && r.value.index == 0x2000 &&
        r.value.sub == 1);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x30, 0, 0, 0, 0, 0, 0, 0}), true);
  CHECK(r.ok() && r.value.kind == SdoReplyKind::SegmentDownload && r.value.toggle);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x20, 0, 0, 0, 0, 0, 0, 0}), true);
  CHECK(r.ok() && r.value.kind == SdoReplyKind::SegmentDownload && !r.value.toggle);
  r = canopen_client::ParseSdoReply(Frame(0x585, {0x80, 0x00, 0x20, 0x01, 0x02, 0x00, 0x01, 0x06}),
                                    true);
  CHECK(r.ok() && r.value.kind == SdoReplyKind::Abort && r.value.size == 0x06010002 &&
        r.value.sub == 1);
}

TEST_CASE("SDO reply decoding rejects what is not a reply") {
  const auto bad = [](const std::vector<uint8_t>& bytes, bool strict) {
    return canopen_client::ParseSdoReply(Frame(0x585, bytes), strict).error == Error::InvalidFrame;
  };
  CHECK(bad({}, true));                                          // no command byte
  CHECK(bad({0x43, 0x00, 0x10, 0x00, 0x92, 0x01, 0x02}, true));  // 7 bytes, strict
  CHECK(bad({0xA0, 0, 0, 0, 0, 0, 0, 0}, true));                 // block download response (5)
  CHECK(bad({0xC0, 0, 0, 0, 0, 0, 0, 0}, true));                 // block upload response (6)
  CHECK(bad({0xE0, 0, 0, 0, 0, 0, 0, 0}, true));                 // reserved (7)
  CHECK(bad({0x80, 0x00, 0x20, 0x01, 0x02, 0x00, 0x01}, false)); // abort without its code
  CHECK(bad({0x60, 0x00, 0x20}, false));                   // initiate reply without a multiplexer
  CHECK(bad({0x43, 0x00, 0x10, 0x00, 0x92, 0x01}, false)); // 4 data bytes announced, 2 present
  CHECK(bad({0x41, 0x08, 0x10, 0x00, 0x0C}, false));       // size indicated, not present
  CHECK(bad({0x00, 'a', 'b'}, false));                     // segment of 7 announced, 2 present
  CHECK(!bad({0x60, 0x00, 0x20, 0x01}, false));            // lenient: 4 bytes suffice
  CHECK(!bad({0x20}, false));                              // lenient: segment response, 1 byte
  CHECK(!bad({0x09, 'x', 'y', 'z'}, false));               // lenient: 3 data bytes, 4 in all
  CHECK(!bad({0x43, 0x00, 0x10, 0x00, 1, 2, 3, 4}, false));
  CanFrame rtr = Frame(0x585, {0x43, 0x00, 0x10, 0x00, 1, 2, 3, 4}, true);
  CHECK(canopen_client::ParseSdoReply(rtr, true).error == Error::InvalidFrame);
  CanFrame long_frame = Frame(0x585, {0x43, 0, 0x10, 0, 1, 2, 3, 4});
  long_frame.dlc = 9;
  CHECK(canopen_client::ParseSdoReply(long_frame, false).error == Error::InvalidFrame);
}

TEST_CASE("Heartbeat and guarding frames decode") {
  auto h = canopen_client::ParseHeartbeat(Frame(0x705, {0x05}));
  CHECK(h.ok() && h.value.node == 5 && h.value.state == canopen_client::NmtState::Operational &&
        !h.value.toggle);
  h = canopen_client::ParseHeartbeat(Frame(0x77F, {0x7F}));
  CHECK(h.ok() && h.value.node == 127 && h.value.state == canopen_client::NmtState::PreOperational);
  h = canopen_client::ParseHeartbeat(Frame(0x701, {0x84}));
  CHECK(h.ok() && h.value.toggle && h.value.state == canopen_client::NmtState::Stopped);
  h = canopen_client::ParseHeartbeat(Frame(0x701, {0x00}));
  CHECK(h.ok() && h.value.state == canopen_client::NmtState::BootUp);
  CHECK(canopen_client::ParseHeartbeat(Frame(0x700, {0x05})).error ==
        Error::InvalidFrame); // node 0
  CHECK(canopen_client::ParseHeartbeat(Frame(0x780, {0x05})).error ==
        Error::InvalidFrame); // above 127
  CHECK(canopen_client::ParseHeartbeat(Frame(0x705, {})).error == Error::InvalidFrame);
  CHECK(canopen_client::ParseHeartbeat(Frame(0x705, {0x05, 0x00})).error == Error::InvalidFrame);
  CHECK(canopen_client::ParseHeartbeat(Frame(0x705, {0x01})).error ==
        Error::InvalidFrame); // not a state
  CHECK(canopen_client::ParseHeartbeat(Frame(0x705, {0x7E})).error == Error::InvalidFrame);
  CHECK(canopen_client::ParseHeartbeat(Frame(0x705, {0x05}, true)).error == Error::InvalidFrame);
}

TEST_CASE("EMCY frames decode") {
  const auto e =
      canopen_client::ParseEmcy(Frame(0x085, {0x10, 0x23, 0x01, 'a', 'b', 'c', 'd', 'e'}));
  CHECK(e.ok() && e.value.node == 5 && e.value.code == 0x2310 && e.value.error_register == 0x01 &&
        e.value.manufacturer[0] == 'a' && e.value.manufacturer[4] == 'e');
  CHECK(canopen_client::ParseEmcy(Frame(0x080, {0, 0, 0, 0, 0, 0, 0, 0})).error ==
        Error::InvalidFrame); // SYNC id
  CHECK(canopen_client::ParseEmcy(Frame(0x100, {0, 0, 0, 0, 0, 0, 0, 0})).error ==
        Error::InvalidFrame);
  CHECK(canopen_client::ParseEmcy(Frame(0x085, {0, 0, 0, 0, 0, 0, 0})).error ==
        Error::InvalidFrame);
  CHECK(canopen_client::ParseEmcy(Frame(0x085, {0, 0, 0, 0, 0, 0, 0, 0}, true)).error ==
        Error::InvalidFrame);
}

TEST_CASE("Abort and NMT state names, enumeration helpers") {
  CHECK(canopen_client::AbortCodeName(0x05030000) == std::string_view{"ToggleNotAlternated"});
  CHECK(canopen_client::AbortCodeName(0x06020000) == std::string_view{"ObjectNotFound"});
  CHECK(canopen_client::AbortCodeName(0x08000024) == std::string_view{"NoData"});
  CHECK(canopen_client::AbortCodeName(0x12345678) == std::string_view{"Unknown"});
  CHECK(canopen_client::AbortCodeName(0) == std::string_view{"Unknown"});
  CHECK(canopen_client::IsNmtCommand(0x01) && canopen_client::IsNmtCommand(0x82));
  CHECK(!canopen_client::IsNmtCommand(0x00) && !canopen_client::IsNmtCommand(0x83));
  canopen_client::NmtState s{};
  CHECK(canopen_client::DecodeNmtState(0x04, s) && s == canopen_client::NmtState::Stopped);
  CHECK(!canopen_client::DecodeNmtState(0x06, s) && s == canopen_client::NmtState::Stopped);
}

TEST_CASE("Every abort code name is distinct and every error has a name") {
  const uint32_t codes[] = {0x05030000, 0x05040000, 0x05040001, 0x05040002, 0x05040003, 0x05040004,
                            0x05040005, 0x06010000, 0x06010001, 0x06010002, 0x06020000, 0x06040041,
                            0x06040042, 0x06040043, 0x06040047, 0x06060000, 0x06070010, 0x06070012,
                            0x06070013, 0x06090011, 0x06090030, 0x06090031, 0x06090032, 0x06090036,
                            0x08000000, 0x08000020, 0x08000021, 0x08000022, 0x08000023, 0x08000024};
  for (std::size_t i = 0; i < std::size(codes); ++i) {
    CHECK(std::string_view{canopen_client::AbortCodeName(codes[i])} != "Unknown");
    for (std::size_t j = i + 1; j < std::size(codes); ++j) {
      CHECK(std::string_view{canopen_client::AbortCodeName(codes[i])} !=
            std::string_view{canopen_client::AbortCodeName(codes[j])});
    }
  }
  for (int v = 0; v < 256; ++v) {
    const auto e = static_cast<Error>(v);
    const bool defined = (v <= 9) || (v >= 16 && v <= 21);
    CHECK((std::string_view{canopen_client::ToString(e)} != "Unknown") == defined);
  }
}

TEST_CASE("PDO helpers: indices, mapping records, validation, bit access") {
  CHECK_EQ(canopen_client::PdoCommunicationIndex(canopen_client::PdoKind::Receive, 1).value,
           0x1400);
  CHECK_EQ(canopen_client::PdoCommunicationIndex(canopen_client::PdoKind::Transmit, 2).value,
           0x1801);
  CHECK_EQ(canopen_client::PdoMappingIndex(canopen_client::PdoKind::Receive, 1).value, 0x1600);
  CHECK_EQ(canopen_client::PdoMappingIndex(canopen_client::PdoKind::Transmit, 512).value,
           0x19FF + 0x200 + 0);
  CHECK(canopen_client::PdoCommunicationIndex(canopen_client::PdoKind::Transmit, 0).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::PdoCommunicationIndex(canopen_client::PdoKind::Transmit, 513).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::PdoMappingIndex(canopen_client::PdoKind::Receive, 0).error ==
        Error::InvalidArgument);
  const canopen_client::PdoMapEntry entry{0x6064, 0, 32};
  CHECK_EQ(canopen_client::EncodePdoMapEntry(entry), 0x60640020U);
  const auto back = canopen_client::DecodePdoMapEntry(0x60410010U);
  CHECK(back.index == 0x6041 && back.sub == 0 && back.bits == 16);
  CHECK(canopen_client::DecodePdoMapEntry(0x20010308U).sub == 3);

  canopen_client::PdoConfig cfg{};
  cfg.cob_id = 0x185;
  cfg.entry_count = 2;
  cfg.entries[0] = {0x6064, 0, 32};
  cfg.entries[1] = {0x6041, 0, 16};
  CHECK(canopen_client::ValidatePdoConfig(cfg) == Error::Ok);
  CHECK_EQ(canopen_client::PdoBitLength(cfg), 48U);
  auto bad = cfg;
  bad.cob_id = 0x180;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument);
  bad.cob_id = 0x580;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument);
  bad = cfg;
  bad.entries[1].bits = 0;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument);
  bad = cfg;
  bad.entries[1].bits = 65;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument);
  bad = cfg;
  bad.entries[0].index = 0;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument);
  bad = cfg;
  bad.entries[0].bits = 64;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument); // 80 bits
  bad = cfg;
  bad.entry_count = 17;
  CHECK(canopen_client::ValidatePdoConfig(bad) == Error::InvalidArgument);
  CHECK(canopen_client::MakePdoFrame(0x185, std::array<uint8_t, 3>{1, 2, 3}).ok());
  CHECK(canopen_client::MakePdoFrame(0x180, std::array<uint8_t, 3>{1, 2, 3}).error ==
        Error::InvalidArgument);
  CHECK(canopen_client::MakePdoFrame(0x185, std::array<uint8_t, 9>{}).error ==
        Error::InvalidArgument);
}

TEST_CASE("PDO bit access is Intel bit order and stays in bounds") {
  std::array<uint8_t, 8> data{0x78, 0x56, 0x34, 0x12, 0xFF, 0x00, 0x80, 0x01};
  CHECK_EQ(canopen_client::ExtractBits(data, 0, 32).value, 0x12345678ULL);
  CHECK_EQ(canopen_client::ExtractBits(data, 4, 8).value, 0x67ULL);
  CHECK_EQ(canopen_client::ExtractBits(data, 8, 4).value, 0x6ULL);
  CHECK_EQ(canopen_client::ExtractBits(data, 55, 1).value, 1ULL);
  CHECK_EQ(canopen_client::ExtractBits(data, 0, 64).value, 0x0180'00FF'1234'5678ULL);
  CHECK(canopen_client::ExtractBits(data, 0, 0).error == Error::InvalidArgument);
  CHECK(canopen_client::ExtractBits(data, 0, 65).error == Error::InvalidArgument);
  CHECK(canopen_client::ExtractBits(data, 60, 8).error == Error::InvalidArgument);
  CHECK(canopen_client::ExtractBits(std::span<const uint8_t>(data).first(2), 8, 9).error ==
        Error::InvalidArgument);
  std::array<uint8_t, 4> out{};
  CHECK(canopen_client::InsertBits(out, 4, 8, 0xAB).ok());
  CHECK(out[0] == 0xB0 && out[1] == 0x0A);
  CHECK(canopen_client::InsertBits(out, 0, 4, 0xFF).ok()); // only the low 4 bits are taken
  CHECK(out[0] == 0xBF);
  CHECK(canopen_client::InsertBits(out, 4, 4, 0x0).ok());
  CHECK(out[0] == 0x0F);
  CHECK(canopen_client::InsertBits(out, 24, 8, 0xFF).ok() && out[3] == 0xFF);
  CHECK(canopen_client::InsertBits(out, 28, 8, 1).error == Error::InvalidArgument);
  CHECK(canopen_client::InsertBits(out, 0, 0, 1).error == Error::InvalidArgument);
  CHECK(canopen_client::InsertBits(out, 0, 65, 1).error == Error::InvalidArgument);
}

TEST_CASE("Statusword decoding uses the 0x4F and 0x6F masks and ignores the other bits") {
  using canopen_client::DecodeStatusword;
  using canopen_client::DriveState;
  CHECK(DecodeStatusword(0x0000) == DriveState::NotReadyToSwitchOn);
  CHECK(DecodeStatusword(0x0240) == DriveState::SwitchOnDisabled);
  CHECK(DecodeStatusword(0x0231) == DriveState::ReadyToSwitchOn);
  CHECK(DecodeStatusword(0x0233) == DriveState::SwitchedOn);
  CHECK(DecodeStatusword(0x0237) == DriveState::OperationEnabled);
  CHECK(DecodeStatusword(0x0217) == DriveState::QuickStopActive);
  CHECK(DecodeStatusword(0x020F) == DriveState::FaultReactionActive);
  CHECK(DecodeStatusword(0x0208) == DriveState::Fault);
  CHECK(DecodeStatusword(0x0288) == DriveState::Fault);            // warning bit 7 set
  CHECK(DecodeStatusword(0x0627) == DriveState::OperationEnabled); // bit 10 (target reached)
  CHECK(DecodeStatusword(0x0001) == DriveState::Unknown);
  CHECK(DecodeStatusword(0x0003) == DriveState::Unknown);
  CHECK(DecodeStatusword(0x0043) == DriveState::Unknown);
  CHECK(DecodeStatusword(0x0063) == DriveState::Unknown); // bit 5 with bit 6: no such state
  CHECK(std::string_view{canopen_client::ToString(DriveState::Fault)} == "Fault");
  CHECK(std::string_view{canopen_client::ToString(DriveState::Unknown)} == "Unknown");
  CHECK(std::string_view{canopen_client::ToString(ut::RawEnum<DriveState>(99))} == "Unknown");
}

TEST_CASE("Enable steps: the controlword that moves one step toward Operation enabled") {
  using canopen_client::DriveState;
  uint16_t w = 0xFFFF;
  CHECK(canopen_client::EnableStepControlword(DriveState::SwitchOnDisabled, w) && w == 0x0006);
  CHECK(canopen_client::EnableStepControlword(DriveState::ReadyToSwitchOn, w) && w == 0x0007);
  CHECK(canopen_client::EnableStepControlword(DriveState::SwitchedOn, w) && w == 0x000F);
  CHECK(canopen_client::EnableStepControlword(DriveState::QuickStopActive, w) && w == 0x0000);
  w = 0x1234;
  CHECK(!canopen_client::EnableStepControlword(DriveState::NotReadyToSwitchOn, w) && w == 0x1234);
  CHECK(!canopen_client::EnableStepControlword(DriveState::OperationEnabled, w));
  CHECK(!canopen_client::EnableStepControlword(DriveState::FaultReactionActive, w));
  CHECK(!canopen_client::EnableStepControlword(DriveState::Fault, w));
  CHECK(!canopen_client::EnableStepControlword(DriveState::Unknown, w));
  CHECK(!canopen_client::EnableStepControlword(ut::RawEnum<DriveState>(99), w) && w == 0x1234);
}

TEST_CASE("Frame validity") {
  CanFrame f;
  CHECK(canopen_client::IsValid(f));
  f.id = 0x7FF;
  f.dlc = 8;
  CHECK(canopen_client::IsValid(f));
  f.id = 0x800;
  CHECK(!canopen_client::IsValid(f));
  f.id = 0x100;
  f.dlc = 9;
  CHECK(!canopen_client::IsValid(f));
}

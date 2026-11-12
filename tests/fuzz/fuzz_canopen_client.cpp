/**
 * @file fuzz_canopen_client.cpp
 * @brief Fuzz target: the driver over a CAN bus whose frames and faults the fuzzer controls.
 *
 * What the driver receives is untrusted, so the bus is the input. The fuzzer's bytes choose a
 * configuration, then a sequence of driver calls with arbitrary arguments; on every receive they
 * also choose what the bus does: pass the node model's frame, deliver a frame the fuzzer wrote (any
 * identifier, length, remote flag and bytes), time out, or fail; on every send they choose whether
 * it fails. They can also change the nodes between calls (silent, aborting, stopped, wrong guard
 * toggle).
 *
 * After every call the target checks the driver's promises (docs/contract.md), and aborts, naming
 * the line, when one breaks:
 *
 * - a failed call returns an `Error` the enum defines and this driver returns (never
 *   `DeviceIdMismatch`, `Crc` or `Busy`), and `T{}` for a scalar `T`;
 * - before `EnsureInitialized()` succeeds, no call sends or receives a frame: every call returns
 *   `NotInitialized`;
 * - `InvalidArgument` means no frame was sent and none was received;
 * - an upload never returns more bytes than the buffer holds;
 * - the EMCY queue never returns more events than it holds, a TPDO sample's length is its
 *   subscription's;
 * - every call returns (a flood of frames cannot hold it), and every log line is a non-empty
 * message.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang,
 * `-DCANOPEN_CLIENT_BUILD_FUZZERS=ON`), and as a plain executable that replays the corpus in
 * tests/fuzz/corpus/ as a CTest on any compiler.
 */
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "canopen_client.hpp"
#include "mock_canopen_client.hpp"

namespace {

using canopen_client::Error;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }
  uint32_t Quad() noexcept {
    const auto high = Word();
    return (static_cast<uint32_t>(high) << 16U) | Word();
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_canopen_client: promise broken at line %d\n", line);
    std::abort();
  }
}

/// What a bus access does, chosen by the fuzzer; any other value passes it to the node model.
constexpr uint8_t kFail = 0xFF;    ///< The access fails.
constexpr uint8_t kInject = 0xFE;  ///< A receive delivers a frame the fuzzer wrote.
constexpr uint8_t kTimeout = 0xFD; ///< A receive times out.

/// The port: the node model of the host tests behind a bus the fuzzer controls.
class FuzzPort : public canopen_client::CanInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  canopen_client_test::SimBus bus;
  int sends = 0;
  int receives = 0;
  int log_lines = 0;
  bool start_ok = true;

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return start_ok;
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    ++sends;
    if (in_.Byte() == kFail) {
      return false;
    }
    bus.FromClient(frame);
    return true;
  }
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    ++receives;
    const uint8_t what = in_.Byte();
    if (what == kFail) {
      return canopen_client::RxStatus::Error;
    }
    if (what == kInject) {
      frame = canopen_client::CanFrame{};
      frame.id =
          static_cast<uint16_t>(in_.Word() & 0x0FFFU);    // 12 bits: some are not valid identifiers
      frame.dlc = static_cast<uint8_t>(in_.Byte() % 11U); // some are not valid lengths
      frame.rtr = (in_.Byte() & 1U) != 0U;
      for (auto& b : frame.data) {
        b = in_.Byte();
      }
      return canopen_client::RxStatus::Frame; // time does not move: the driver must bound the wait
    }
    const uint64_t deadline = bus.now_us + timeout_us;
    bus.PumpHeartbeats(deadline);
    if (what != kTimeout && !bus.rx.empty() && bus.rx.front().at_us <= deadline) {
      frame = bus.rx.front().frame;
      bus.now_us = (bus.rx.front().at_us > bus.now_us) ? bus.rx.front().at_us : bus.now_us;
      bus.rx.erase(bus.rx.begin());
      return canopen_client::RxStatus::Frame;
    }
    bus.now_us = deadline;
    return canopen_client::RxStatus::Timeout;
  }
  [[nodiscard]] uint32_t NowUs() const noexcept {
    return static_cast<uint32_t>(bus.now_us & 0xFFFF'FFFFU);
  }
  void Log(canopen_client::LogLevel level, const char* message) noexcept {
    (void)level;
    Check((message != nullptr) && (message[0] != '\0'));
    ++log_lines;
  }

private:
  Input& in_;
};

/// A configuration from the input: some valid, some not (a refused one must leave the port alone).
canopen_client::Config ConfigFrom(Input& in) noexcept {
  canopen_client::Config c{};
  const uint8_t kind = in.Byte();
  if ((kind & 1U) != 0U) {
    c.sdo_timeout_us = 1'000U + (static_cast<uint32_t>(in.Byte()) * 400U);
    c.guard_timeout_us = 1'000U + (static_cast<uint32_t>(in.Byte()) * 400U);
    c.drive_timeout_us = 1'000U + (static_cast<uint32_t>(in.Byte()) * 2'000U);
    c.drive_poll_interval_us = 100U + (static_cast<uint32_t>(in.Byte()) * 100U);
    c.safety_attempts = static_cast<uint8_t>(1U + (in.Byte() % 8U));
  }
  c.abort_on_timeout = (kind & 2U) != 0U;
  c.strict_sdo_length = (kind & 4U) == 0U;
  if ((kind & 8U) != 0U) {
    c.sdo_timeout_us = in.Quad(); // anything: Validate() decides
  }
  return c;
}

bool AllowedError(Error e) noexcept {
  return (e != Error::DeviceIdMismatch) && (e != Error::Crc) && (e != Error::Busy) &&
         (static_cast<uint8_t>(e) <= 9U ||
          (static_cast<uint8_t>(e) >= 16U && static_cast<uint8_t>(e) <= 21U));
}

/// The driver under test, the bookkeeping, and the checks every call goes through.
class Harness {
public:
  explicit Harness(Input& in) noexcept : in_(in), port_(in), client_(port_, ConfigFrom(in)) {
    port_.bus.AddNode(5);
    port_.bus.AddNode(6);
    port_.start_ok = (in.Byte() & 1U) == 0U;
  }

  void Run() noexcept {
    int calls = 0;
    while (!in_.Empty() && (calls++ < 400)) {
      step(in_.Byte());
    }
  }

private:
  /// Common checks of one call's outcome. `touch` is whether the call may touch the bus at all.
  template <typename R>
  void after(const R& r, int sends0, int receives0) noexcept {
    Check(AllowedError(r.error));
    if (!r.ok()) {
      if constexpr (requires { r.value; }) {
        if constexpr (std::is_arithmetic_v<decltype(r.value)> ||
                      std::is_enum_v<decltype(r.value)>) {
          Check(r.value == decltype(r.value){});
        }
      }
    }
    if (!started_) {
      Check(r.error == Error::NotInitialized);
      Check((port_.sends == sends0) && (port_.receives == receives0));
    }
    if (r.error == Error::InvalidArgument) {
      Check((port_.sends == sends0) && (port_.receives == receives0));
    }
  }

  template <typename Fn>
  void call(Fn fn) noexcept {
    const int sends0 = port_.sends;
    const int receives0 = port_.receives;
    const auto r = fn();
    after(r, sends0, receives0);
  }

  /// A call that must reach the wire whatever happened before (disable, quick stop, NMT stop): once
  /// it is past argument and start-up checks it attempts at least one send, even when the port's
  /// receive side is failing.
  template <typename Fn>
  void safety(Fn fn) noexcept {
    const int sends0 = port_.sends;
    const int receives0 = port_.receives;
    const auto r = fn();
    after(r, sends0, receives0);
    if (started_ && (r.error != Error::InvalidArgument) && (r.error != Error::NotInitialized)) {
      Check(port_.sends > sends0);
    }
  }

  /// A call that is allowed before start-up (`EnsureInitialized()`, `SetConfig()`).
  template <typename Fn>
  void plain(Fn fn) noexcept {
    const int sends0 = port_.sends;
    const int receives0 = port_.receives;
    const auto r = fn();
    Check(AllowedError(r.error));
    if (r.error == Error::InvalidArgument) {
      Check((port_.sends == sends0) && (port_.receives == receives0));
    }
  }

  uint8_t pickNode() noexcept {
    const uint8_t b = in_.Byte();
    return (b < 4U) ? static_cast<uint8_t>(b * 60U)
                    : static_cast<uint8_t>(5U + (b & 1U)); // 0, 60, 120, 180 or 5, 6
  }

  canopen_client::DriveObjects objects() noexcept {
    canopen_client::DriveObjects o{};
    if (in_.Byte() % 8U == 0U) {
      o.controlword = in_.Word();
      o.statusword = in_.Word();
    }
    return o;
  }

  canopen_client::PdoConfig pdoFrom() noexcept {
    canopen_client::PdoConfig c{};
    c.cob_id = static_cast<uint16_t>(0x180U + (in_.Word() % 0x500U));
    c.transmission_type = in_.Byte();
    c.inhibit_time_100us = (in_.Byte() & 1U) != 0U ? in_.Word() : uint16_t{0};
    c.event_timer_ms = (in_.Byte() & 1U) != 0U ? in_.Word() : uint16_t{0};
    c.enabled = (in_.Byte() & 1U) != 0U;
    c.entry_count = static_cast<uint8_t>(in_.Byte() % 20U);
    for (auto& e : c.entries) {
      e = {in_.Word(), in_.Byte(), static_cast<uint8_t>(in_.Byte() % 70U)};
    }
    return c;
  }

  void fault() noexcept {
    canopen_client_test::SimNode& n = port_.bus.nodes[in_.Byte() % 2U];
    switch (in_.Byte() % 8U) {
    case 0:
      n.responds = !n.responds;
      break;
    case 1:
      n.guarding = !n.guarding;
      break;
    case 2:
      n.abort_next_with = in_.Quad();
      break;
    case 3:
      if (in_.Byte() % 3U == 0U) {
        n.nmt = 4U;
      } else if (in_.Byte() % 2U == 0U) {
        n.nmt = 5U;
      } else {
        n.nmt = 0x7FU;
      }
      break;
    case 4:
      n.guard_toggle = !n.guard_toggle;
      break;
    case 5:
      n.supports_segmented = !n.supports_segmented;
      break;
    case 6:
      port_.bus.Emcy(n.id, in_.Word(), in_.Byte());
      break;
    default:
      n.drive.state = static_cast<canopen_client_test::SimDrive::State>(in_.Byte() % 8U);
      n.drive.pending = n.drive.state;
      break;
    }
  }

  void step(uint8_t op) noexcept {
    using canopen_client::NmtCommand;
    using canopen_client::PdoKind;
    switch (op % 39U) {
    case 0:
      plain([&] { return client_.EnsureInitialized(); });
      break;
    case 1:
      call([&] { return client_.Poll(in_.Byte() * 400U); });
      break;
    case 2: {
      const auto command = std::bit_cast<NmtCommand>(static_cast<uint8_t>(in_.Byte() % 0x84U));
      const uint8_t node = pickNode();
      if (command == NmtCommand::Stop) {
        safety([&] { return client_.SendNmt(command, node); });
      } else {
        call([&] { return client_.SendNmt(command, node); });
      }
      break;
    }
    case 3:
      call([&] { return client_.SendSync(in_.Byte()); });
      break;
    case 4: {
      std::array<uint8_t, 10> d{};
      for (auto& b : d) {
        b = in_.Byte();
      }
      const std::size_t n = in_.Byte() % 11U;
      call([&] {
        return client_.SendRpdo(in_.Word() % 0x800U, std::span<const uint8_t>(d.data(), n));
      });
      break;
    }
    case 5:
      call([&] { return client_.MonitorNode(pickNode(), in_.Quad() % 70'000'000U); });
      break;
    case 6:
      call([&] { return client_.UnmonitorNode(pickNode()); });
      break;
    case 7:
      call([&] { return client_.GetNodeStatus(pickNode()); });
      break;
    case 8:
      call([&] { return client_.GuardNode(pickNode()); });
      break;
    case 9: {
      canopen_client::EmcyEvent e{};
      std::size_t popped = 0;
      while (client_.PopEmcy(e)) {
        Check(++popped <= canopen_client::kEmcyQueueCapacity);
      }
      break;
    }
    case 10:
      call([&] { return client_.SubscribeTpdo(in_.Word() % 0x600U, in_.Byte() % 10U); });
      break;
    case 11:
      call([&] { return client_.UnsubscribeTpdo(in_.Word() % 0x600U); });
      break;
    case 12: {
      const uint16_t id = in_.Word() % 0x600U;
      const auto r = client_.ReadTpdo(id);
      Check(AllowedError(r.error));
      if (r.ok()) {
        Check(r.value.cob_id == id && r.value.dlc <= canopen_client::kMaxDlc);
      }
      break;
    }
    case 13: {
      std::array<uint8_t, 16> buf{};
      const std::size_t n = in_.Byte() % 17U;
      const uint8_t node = pickNode();
      const uint16_t index = in_.Byte() % 2U == 0U ? uint16_t{0x2100} : in_.Word();
      const auto r =
          client_.SdoUpload(node, index, in_.Byte() % 2U, std::span<uint8_t>(buf.data(), n));
      Check(AllowedError(r.error));
      Check(!r.ok() || (r.value <= n));
      break;
    }
    case 14: {
      std::array<uint8_t, 24> d{};
      for (auto& b : d) {
        b = in_.Byte();
      }
      const std::size_t n = in_.Byte() % 25U;
      const uint8_t node = pickNode();
      const uint16_t index = in_.Byte() % 2U == 0U ? uint16_t{0x2100} : in_.Word();
      call([&] {
        return client_.SdoDownload(node, index, in_.Byte() % 2U,
                                   std::span<const uint8_t>(d.data(), n));
      });
      break;
    }
    case 15:
      call([&] { return client_.SdoReadU8(pickNode(), in_.Word(), in_.Byte() % 3U); });
      break;
    case 16:
      call([&] { return client_.SdoReadU16(pickNode(), in_.Word(), in_.Byte() % 3U); });
      break;
    case 17:
      call([&] { return client_.SdoReadU32(pickNode(), in_.Word(), in_.Byte() % 3U); });
      break;
    case 18:
      call(
          [&] { return client_.SdoWriteU32(pickNode(), in_.Word(), in_.Byte() % 3U, in_.Quad()); });
      break;
    case 19: {
      const std::array<uint8_t, 4> d{in_.Byte(), in_.Byte(), in_.Byte(), in_.Byte()};
      const std::size_t n = in_.Byte() % 6U;
      call([&] {
        return client_.SdoDownloadUnconfirmed(pickNode(), in_.Word(), in_.Byte() % 3U,
                                              std::span<const uint8_t>(d.data(), n));
      });
      break;
    }
    case 20:
      call([&] { return client_.StoreParameters(pickNode(), in_.Byte() % 6U); });
      break;
    case 21:
      call([&] { return client_.WriteHeartbeatProducerTime(pickNode(), in_.Word() % 50U); });
      break;
    case 22:
      call([&] { return client_.ConfigureSync(in_.Quad() % 2'000'000U, in_.Byte() % 243U); });
      break;
    case 23:
      call([&] { return client_.ServiceSync(); });
      break;
    case 24: {
      const auto cfg = pdoFrom();
      const PdoKind kind = (in_.Byte() & 1U) != 0U ? PdoKind::Transmit : PdoKind::Receive;
      call([&] { return client_.ConfigurePdo(pickNode(), kind, in_.Byte() % 4U, cfg); });
      break;
    }
    case 25:
      call([&] { return client_.ReadPdoConfig(pickNode(), PdoKind::Transmit, in_.Byte() % 4U); });
      break;
    case 26:
      call([&] { return client_.DisablePdo(pickNode(), PdoKind::Receive, in_.Byte() % 4U); });
      break;
    case 27:
      call([&] { return client_.ReadDriveState(pickNode(), objects()); });
      break;
    case 28:
      call([&] { return client_.DriveEnable(pickNode(), objects()); });
      break;
    case 29:
      safety([&] { return client_.DriveDisable(pickNode(), objects()); });
      break;
    case 30:
      safety([&] { return client_.DriveQuickStop(pickNode(), objects()); });
      break;
    case 31:
      call([&] { return client_.DriveResetFault(pickNode(), objects()); });
      break;
    case 32: {
      const auto mode = static_cast<canopen_client::OperationMode>(static_cast<int8_t>(in_.Byte()));
      const auto verify =
          (in_.Byte() & 1U) != 0U ? canopen_client::Verify::ReadBack : canopen_client::Verify::None;
      call([&] { return client_.SetDriveMode(pickNode(), mode, objects(), verify); });
      break;
    }
    case 33:
      plain([&] { return client_.SetConfig(ConfigFrom(in_)); });
      break;
    case 34:
      fault();
      break;
    case 35: {
      canopen_client::CanFrame f{};
      f.id = static_cast<uint16_t>(in_.Word() & 0x0FFFU);
      f.dlc = static_cast<uint8_t>(in_.Byte() % 11U);
      call([&] { return client_.SendRawFrame(f); });
      break;
    }
    case 36:
      call([&] { return client_.ReadRawFrame(in_.Byte() * 100U); });
      break;
    case 37:
      call([&] { return client_.ReadRawSdo(pickNode(), in_.Word(), in_.Byte() % 3U); });
      break;
    default: {
      const auto r = client_.Deinitialize();
      Check(r.ok());
      break;
    }
    }
    started_ = client_.IsInitialized();
    const canopen_client::Stats& s = client_.GetStats();
    Check(s.frames_invalid <= s.frames_received);
  }

  Input& in_;
  FuzzPort port_;
  canopen_client::Client<FuzzPort> client_;
  bool started_ = false;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  if (size > 4096U) {
    return 0;
  }
  Input in(data, size);
  Harness harness(in);
  harness.Run();
  return 0;
}

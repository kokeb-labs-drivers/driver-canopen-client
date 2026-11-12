/**
 * @file mock_canopen_client.hpp
 * @brief A model of a CAN bus with CANopen nodes, and the mock ports.
 *
 * The model is written from the protocol's byte layouts, not from the driver's codecs: it has its
 * own SDO server (expedited, segmented, abort rules), NMT and guarding state, heartbeat producer,
 * EMCY, a PDO configuration with the profile's update rules, TPDO and RPDO handling, and a CiA 402
 * drive state machine decoded from controlword masks. It shares no code with `inc/`, so a bug in a
 * driver encoder is not mirrored here.
 *
 * Time is virtual: it advances only inside `Receive()` (to the next frame or the end of the wait).
 * Faults are injected through public fields and the `mutate` hook.
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `MockPort` | `EnsureInitialized`, `Send`, `Receive`, `NowUs` |
 * | `LoggingPort` | the same plus `Log()` |
 * | `TxOnlyPort` | `EnsureInitialized` and `Send` only: the transmit-only mode |
 */
#ifndef CANOPEN_CLIENT_MOCK_CANOPEN_CLIENT_HPP
#define CANOPEN_CLIENT_MOCK_CANOPEN_CLIENT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "canopen_client_can_interface.hpp"
#include "canopen_client_types.hpp"

namespace canopen_client_test {

using canopen_client::CanFrame;
using canopen_client::RxStatus;

/// One object dictionary entry of a node.
struct Object {
  uint16_t index = 0;
  uint8_t sub = 0;
  std::vector<uint8_t> value;
  bool readable = true;
  bool writable = true;
  bool domain = false;     ///< variable length up to `max_len`, transferred segmented
  std::size_t max_len = 0; ///< capacity of a domain object
};

/// A little-endian value of `size` bytes.
inline std::vector<uint8_t> Le(uint32_t v, std::size_t size) {
  std::vector<uint8_t> out(size);
  for (std::size_t i = 0; i < size; ++i) {
    out[i] = static_cast<uint8_t>((v >> (8U * i)) & 0xFFU);
  }
  return out;
}

inline uint32_t FromLe(const std::vector<uint8_t>& v) {
  uint32_t r = 0;
  for (std::size_t i = 0; i < v.size() && i < 4; ++i) {
    r |= static_cast<uint32_t>(v[i]) << (8U * i);
  }
  return r;
}

class SimBus;

/// The CiA 402 state machine of a simulated drive, decoded from controlword masks.
struct SimDrive {
  enum class State : uint8_t {
    NotReady,
    Disabled,
    Ready,
    Switched,
    Enabled,
    QuickStop,
    FaultReaction,
    Fault
  };
  State state = State::Disabled;
  State pending = State::Disabled;
  uint64_t pending_at_us = 0;
  uint64_t transition_delay_us = 0; ///< a state change shows in the statusword after this
  uint64_t fault_reaction_us = 500;
  uint16_t controlword = 0;
  uint16_t extra_status_bits = 0x0280; ///< remote and warning bits: the driver must ignore them
  bool fault_persists = false;         ///< a fault reset does not clear it
  bool quick_stop_to_enabled = false;  ///< transition 16 is implemented
  int8_t mode = 0;
  int8_t mode_display = 0;
  uint64_t mode_display_at_us = 0;
  uint64_t mode_latency_us = 0;
  int8_t mode_display_pending = 0;
  int writes = 0;

  void Apply(uint64_t now) {
    if (state == State::FaultReaction && now >= pending_at_us) {
      state = State::Fault;
    } else if (pending != state && now >= pending_at_us && state != State::FaultReaction) {
      state = pending;
    } else {
      // nothing due
    }
    if (mode_display != mode_display_pending && now >= mode_display_at_us) {
      mode_display = mode_display_pending;
    }
  }
  void Goto(State s, uint64_t now) {
    pending = s;
    pending_at_us = now + transition_delay_us;
    if (transition_delay_us == 0) {
      state = s;
    }
  }
  void RaiseFault(uint64_t now) {
    state = State::FaultReaction;
    pending = State::Fault;
    pending_at_us = now + fault_reaction_us;
  }
  uint16_t Statusword(uint64_t now) {
    Apply(now);
    uint32_t base = 0;
    switch (state) {
    case State::NotReady:
      base = 0x0000;
      break;
    case State::Disabled:
      base = 0x0040;
      break;
    case State::Ready:
      base = 0x0021;
      break;
    case State::Switched:
      base = 0x0023;
      break;
    case State::Enabled:
      base = 0x0027;
      break;
    case State::QuickStop:
      base = 0x0007;
      break;
    case State::FaultReaction:
      base = 0x000F;
      break;
    case State::Fault:
      base = 0x0008;
      break;
    default:
      break;
    }
    const bool powered =
        (state == State::Ready || state == State::Switched || state == State::Enabled);
    const uint32_t voltage = powered ? 0x0010U : 0U;
    return static_cast<uint16_t>(base | voltage | static_cast<uint32_t>(extra_status_bits));
  }
  /// The command the controlword encodes, by the profile's bit masks.
  void WriteControlword(uint16_t cw, uint64_t now) {
    ++writes;
    Apply(now);
    const uint16_t old = controlword;
    controlword = cw;
    if (state == State::Fault) {
      const bool rising = ((cw & 0x80U) != 0) && ((old & 0x80U) == 0);
      if (rising && !fault_persists) {
        Goto(State::Disabled, now);
      }
      return;
    }
    if (state == State::FaultReaction) {
      return;
    }
    const bool disable_voltage = (cw & 0x82U) == 0x00U;
    const bool quick = (cw & 0x86U) == 0x02U;
    const bool shutdown = (cw & 0x87U) == 0x06U;
    const bool switch_on = (cw & 0x8FU) == 0x07U;
    const bool enable = (cw & 0x8FU) == 0x0FU;
    switch (state) {
    case State::Disabled:
      if (shutdown) {
        Goto(State::Ready, now);
      }
      break;
    case State::Ready:
      if (disable_voltage || quick) {
        Goto(State::Disabled, now);
      } else if (switch_on || enable) {
        Goto(State::Switched, now);
      }
      break;
    case State::Switched:
      if (disable_voltage || quick) {
        Goto(State::Disabled, now);
      } else if (shutdown) {
        Goto(State::Ready, now);
      } else if (enable) {
        Goto(State::Enabled, now);
      }
      break;
    case State::Enabled:
      if (disable_voltage) {
        Goto(State::Disabled, now);
      } else if (quick) {
        Goto(State::QuickStop, now);
      } else if (shutdown) {
        Goto(State::Ready, now);
      } else if (switch_on) {
        Goto(State::Switched, now);
      }
      break;
    case State::QuickStop:
      if (disable_voltage) {
        Goto(State::Disabled, now);
      } else if (enable && quick_stop_to_enabled) {
        Goto(State::Enabled, now);
      }
      break;
    case State::NotReady:
    case State::FaultReaction:
    case State::Fault:
    default:
      break;
    }
  }
  void WriteMode(int8_t m, uint64_t now) {
    mode = m;
    mode_display_pending = m;
    mode_display_at_us = now + mode_latency_us;
    if (mode_latency_us == 0) {
      mode_display = m;
    }
  }
};

/// One simulated CANopen node.
class SimNode {
public:
  explicit SimNode(uint8_t node_id) : id(node_id) {
    Add(0x1000, 0, Le(0x00020192, 4), true, false); // device type
    Add(0x1001, 0, Le(0, 1), true, false);          // error register
    Add(0x1017, 0, Le(0, 2), true, true);           // heartbeat producer time
    Add(0x1010, 1, Le(1, 4), true, true);           // store parameters, "save" signature
    Add(0x1011, 1, Le(1, 4), true, true);           // restore defaults, "load" signature
    Add(0x2000, 0, Le(0x11223344, 4), true, true);  // 32-bit manufacturer object
    Add(0x2001, 0, Le(0xABCD, 2), true, true);      // 16-bit
    Add(0x2002, 0, Le(0x5A, 1), true, true);        // 8-bit
    Add(0x2003, 0, Le(0xDEADBEEF, 4), true, false); // read-only
    Add(0x2004, 0, Le(0xCAFE, 2), false, true);     // write-only
    Object str{0x2100, 0, {'C', 'A', 'N', 'o', 'p', 'e', 'n', ' ', 'n', 'o', 'd', 'e'}, true, true,
               true,   64};
    od.push_back(str); // domain object, segmented
    Object name{0x1008, 0, {'S', 'i', 'm'}, true, false, true, 64};
    od.push_back(name);
    for (uint16_t n = 0; n < 2; ++n) { // two RPDO and two TPDO records
      AddPdoRecord(static_cast<uint16_t>(0x1400 + n), static_cast<uint16_t>(0x1600 + n),
                   0x200U + (0x100U * n) + id, true);
      AddPdoRecord(static_cast<uint16_t>(0x1800 + n), static_cast<uint16_t>(0x1A00 + n),
                   0x180U + (0x100U * n) + id, false);
    }
    Add(0x6040, 0, Le(0, 2), true, true);
    Add(0x6041, 0, Le(0, 2), true, false);
    Add(0x6060, 0, Le(0, 1), true, true);
    Add(0x6061, 0, Le(0, 1), true, false);
  }

  uint8_t id;
  uint8_t nmt = 0x7F; ///< 0 boot-up, 4 stopped, 5 operational, 0x7F pre-operational
  std::vector<Object> od;
  SimDrive drive;
  uint16_t drive_offset = 0; ///< a second axis lives at 0x6040 + offset
  // SDO server state
  enum class Sdo : uint8_t { Idle, Up, Down } sdo = Sdo::Idle;
  bool sdo_toggle = false;
  std::vector<uint8_t> sdo_buf;
  std::size_t sdo_pos = 0;
  uint16_t sdo_index = 0;
  uint8_t sdo_sub = 0;
  std::size_t sdo_expected = 0;
  // Behaviour switches
  bool responds = true; ///< false: the node never answers an SDO
  bool guarding = true; ///< answers remote frames
  bool guard_toggle = false;
  bool supports_segmented = true;
  uint32_t abort_next_with = 0; ///< abort the next SDO request with this code
  int sdo_requests = 0;
  int aborts_received = 0;
  uint32_t last_abort_code = 0;
  std::vector<std::vector<uint8_t>> rpdo_log;

  Object* Find(uint16_t index, uint8_t sub) {
    for (Object& o : od) {
      if (o.index == index && o.sub == sub) {
        return &o;
      }
    }
    return nullptr;
  }
  [[nodiscard]] bool HasIndex(uint16_t index) const {
    return std::any_of(od.begin(), od.end(), [index](const Object& o) { return o.index == index; });
  }
  void Add(uint16_t index, uint8_t sub, std::vector<uint8_t> value, bool r, bool w) {
    Object o;
    o.index = index;
    o.sub = sub;
    o.value = std::move(value);
    o.readable = r;
    o.writable = w;
    od.push_back(o);
  }
  /// Move the drive objects to another axis (index offset).
  void MoveDrive(uint16_t offset) {
    for (Object& o : od) {
      if (o.index >= 0x6040 && o.index <= 0x6061 && offset != 0) {
        o.index = static_cast<uint16_t>(o.index + offset);
      }
    }
    drive_offset = offset;
  }

  uint32_t Value(uint16_t index, uint8_t sub) {
    const Object* o = Find(index, sub);
    return o ? FromLe(o->value) : 0;
  }

  void AddPdoRecord(uint16_t comm, uint16_t map, uint32_t cob, bool rx) {
    Add(comm, 0, Le(rx ? 2 : 6, 1), true, false);
    Add(comm, 1, Le(cob | 0x80000000U, 4), true, true); // invalid until configured
    Add(comm, 2, Le(0xFF, 1), true, true);
    if (!rx) {
      Add(comm, 3, Le(0, 2), true, true);
      Add(comm, 5, Le(0, 2), true, true);
    }
    Add(map, 0, Le(0, 1), true, true);
    for (uint8_t i = 1; i <= 8; ++i) {
      Add(map, i, Le(0, 4), true, true);
    }
  }
};

/// A frame the bus delivers to the client at a time.
struct Queued {
  uint64_t at_us;
  CanFrame frame;
};

/// The bus, its nodes, time and fault injection.
class SimBus {
public:
  uint64_t now_us = 1'000'000;
  std::vector<CanFrame> sent; ///< every frame the client sent
  std::vector<Queued> rx;     ///< frames on their way to the client (sorted by time)
  std::deque<SimNode> nodes;  ///< a deque: references to nodes stay valid when more are added
  uint64_t reply_latency_us = 200;
  // Port and bus faults
  bool init_ok = true;
  bool send_ok = true;
  int fail_send_index = -1;    ///< fail the send with this 0-based index
  int fail_receive_index = -1; ///< fail the receive with this 0-based index (a controller error)
  bool receive_error = false;
  int init_calls = 0;
  int sends = 0;
  const CanFrame* last_sent_ptr = nullptr; ///< the address the port was given on the last Send
  int receives = 0;
  /// Change or drop a node's reply before it is queued: set `drop`, or edit the frame.
  std::function<void(CanFrame&, bool& drop)> mutate;
  /// The previous node reply is delivered again after this delay (a duplicate); 0 off.
  uint64_t duplicate_after_us = 0;
  std::vector<std::string> logs;

  SimNode& AddNode(uint8_t id) {
    nodes.emplace_back(id);
    hb_next_.push_back(0);
    return nodes.back();
  }
  SimNode* Node(uint8_t id) {
    for (SimNode& n : nodes) {
      if (n.id == id) {
        return &n;
      }
    }
    return nullptr;
  }

  /// Queue a frame for the client at `at_us` (kept sorted, stable).
  void Inject(CanFrame f, uint64_t at_us) {
    Queued q{at_us, f};
    auto it = std::upper_bound(rx.begin(), rx.end(), q,
                               [](const Queued& a, const Queued& b) { return a.at_us < b.at_us; });
    rx.insert(it, q);
  }
  /// A node frame to the client, after the reply latency, through the fault hook.
  void Reply(CanFrame f, uint64_t extra_us = 0) {
    bool drop = false;
    if (mutate) {
      mutate(f, drop);
    }
    if (drop) {
      return;
    }
    Inject(f, now_us + reply_latency_us + extra_us);
    if (duplicate_after_us != 0) {
      Inject(f, now_us + reply_latency_us + extra_us + duplicate_after_us);
    }
  }

  /// A heartbeat producer ticks while time passes: queue the frames due up to `until_us`.
  void PumpHeartbeats(uint64_t until_us) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      SimNode& n = nodes[i];
      const uint32_t period_ms = n.Value(0x1017, 0);
      if (period_ms == 0) {
        hb_next_[i] = 0;
        continue;
      }
      const uint64_t period = static_cast<uint64_t>(period_ms) * 1000U;
      if (hb_next_[i] == 0) {
        hb_next_[i] = now_us + period;
      }
      while (hb_next_[i] <= until_us) {
        CanFrame f;
        f.id = static_cast<uint16_t>(0x700 + n.id);
        f.dlc = 1;
        f.data[0] = n.nmt;
        Inject(f, hb_next_[i]);
        hb_next_[i] += period;
      }
    }
  }

  // ---- the client's frames arrive here -----------------------------------------------------
  void FromClient(const CanFrame& f) {
    sent.push_back(f);
    if (f.id == 0x000 && f.dlc == 2 && !f.rtr) {
      nmt(f.data[0], f.data[1]);
    } else if (f.id == 0x080 && !f.rtr) {
      sync();
    } else if (f.id >= 0x601 && f.id <= 0x67F && !f.rtr) {
      sdoRequest(static_cast<uint8_t>(f.id - 0x600), f);
    } else if (f.id >= 0x701 && f.id <= 0x77F && f.rtr) {
      guard(static_cast<uint8_t>(f.id - 0x700));
    } else if (!f.rtr) {
      for (SimNode& n : nodes) {
        rpdo(n, f);
      }
    } else {
      // another remote frame: nobody answers
    }
  }

  /// Raise an emergency on a node.
  void Emcy(uint8_t node, uint16_t code, uint8_t reg, std::vector<uint8_t> mfr = {}) {
    SimNode* n = Node(node);
    if (n == nullptr) {
      return;
    }
    n->Find(0x1001, 0)->value = Le(reg, 1);
    CanFrame f;
    f.id = static_cast<uint16_t>(0x080 + node);
    f.dlc = 8;
    f.data[0] = static_cast<uint8_t>(code & 0xFF);
    f.data[1] = static_cast<uint8_t>(code >> 8);
    f.data[2] = reg;
    for (std::size_t i = 0; i < 5 && i < mfr.size(); ++i) {
      f.data[3 + i] = mfr[i];
    }
    Reply(f);
  }
  /// Send the boot-up message of a node.
  void BootUp(SimNode& n) {
    n.nmt = 0;
    CanFrame f;
    f.id = static_cast<uint16_t>(0x700 + n.id);
    f.dlc = 1;
    f.data[0] = 0;
    Reply(f);
    n.nmt = 0x7F;
    n.guard_toggle = false;
    n.sdo = SimNode::Sdo::Idle;
  }

private:
  std::vector<uint64_t> hb_next_;

  void nmt(uint8_t cmd, uint8_t target) {
    for (SimNode& n : nodes) {
      if (target != 0 && target != n.id) {
        continue;
      }
      switch (cmd) {
      case 0x01:
        n.nmt = 5;
        break;
      case 0x02:
        n.nmt = 4;
        break;
      case 0x80:
        n.nmt = 0x7F;
        break;
      case 0x81:
      case 0x82:
        BootUp(n);
        break;
      default:
        break;
      }
    }
  }

  void sync() {
    for (SimNode& n : nodes) {
      if (n.nmt != 5) {
        continue;
      }
      for (uint16_t t = 0; t < 2; ++t) {
        const auto comm = static_cast<uint16_t>(0x1800 + t);
        const auto map = static_cast<uint16_t>(0x1A00 + t);
        const uint32_t cob = n.Value(comm, 1);
        const uint32_t type = n.Value(comm, 2);
        if ((cob & 0x80000000U) != 0 || type < 1 || type > 240) {
          continue;
        }
        CanFrame f;
        f.id = static_cast<uint16_t>(cob & 0x7FFU);
        const uint32_t count = n.Value(map, 0);
        std::size_t pos = 0;
        for (uint32_t i = 1; i <= count && pos < 8; ++i) {
          const uint32_t rec = n.Value(map, static_cast<uint8_t>(i));
          const Object* o =
              n.Find(static_cast<uint16_t>(rec >> 16), static_cast<uint8_t>((rec >> 8) & 0xFF));
          const std::size_t bytes = (rec & 0xFF) / 8U;
          for (std::size_t b = 0; o != nullptr && b < bytes && pos < 8; ++b) {
            f.data[pos++] = b < o->value.size() ? o->value[b] : 0;
          }
        }
        f.dlc = static_cast<uint8_t>(pos);
        Reply(f);
      }
    }
  }

  void rpdo(SimNode& n, const CanFrame& f) {
    if (n.nmt != 5) {
      return;
    }
    for (uint16_t r = 0; r < 2; ++r) {
      const auto comm = static_cast<uint16_t>(0x1400 + r);
      const auto map = static_cast<uint16_t>(0x1600 + r);
      const uint32_t cob = n.Value(comm, 1);
      if ((cob & 0x80000000U) != 0 || (cob & 0x7FFU) != f.id) {
        continue;
      }
      n.rpdo_log.emplace_back(f.data.begin(), f.data.begin() + f.dlc);
      const uint32_t count = n.Value(map, 0);
      std::size_t pos = 0;
      for (uint32_t i = 1; i <= count; ++i) {
        const uint32_t rec = n.Value(map, static_cast<uint8_t>(i));
        Object* o =
            n.Find(static_cast<uint16_t>(rec >> 16), static_cast<uint8_t>((rec >> 8) & 0xFF));
        const std::size_t bytes = (rec & 0xFF) / 8U;
        if (o != nullptr && pos + bytes <= f.dlc && bytes == o->value.size()) {
          std::copy(f.data.begin() + pos, f.data.begin() + pos + bytes, o->value.begin());
        }
        pos += bytes;
      }
    }
  }

  void guard(uint8_t node) {
    SimNode* n = Node(node);
    if (n == nullptr || !n->guarding) {
      return;
    }
    CanFrame f;
    f.id = static_cast<uint16_t>(0x700 + node);
    f.dlc = 1;
    f.data[0] = static_cast<uint8_t>(n->nmt | (n->guard_toggle ? 0x80U : 0U));
    n->guard_toggle = !n->guard_toggle;
    Reply(f);
  }

  // ---- SDO server --------------------------------------------------------------------------
  void sdoAbort(SimNode& n, uint16_t index, uint8_t sub, uint32_t code) {
    CanFrame f;
    f.id = static_cast<uint16_t>(0x580 + n.id);
    f.dlc = 8;
    f.data[0] = 0x80;
    f.data[1] = static_cast<uint8_t>(index & 0xFF);
    f.data[2] = static_cast<uint8_t>(index >> 8);
    f.data[3] = sub;
    for (std::size_t i = 0; i < 4U; ++i) {
      f.data[4U + i] = static_cast<uint8_t>((code >> (8U * i)) & 0xFFU);
    }
    n.sdo = SimNode::Sdo::Idle;
    Reply(f);
  }
  CanFrame sdoFrame(const SimNode& n) {
    CanFrame f;
    f.id = static_cast<uint16_t>(0x580 + n.id);
    f.dlc = 8;
    return f;
  }

  void readHook(SimNode& n, Object& o) const {
    const auto base = static_cast<uint16_t>(0x6040 + n.drive_offset);
    if (o.index == static_cast<uint16_t>(base + 1) && o.sub == 0) {
      o.value = Le(n.drive.Statusword(now_us), 2);
    } else if (o.index == static_cast<uint16_t>(base + 0x21) && o.sub == 0) {
      n.drive.Apply(now_us);
      o.value = Le(static_cast<uint8_t>(n.drive.mode_display), 1);
    } else {
      // plain object
    }
  }

  uint32_t writeRule(SimNode& n, const Object& o, const std::vector<uint8_t>& data) {
    const uint16_t idx = o.index;
    const bool rpdo_map = idx >= 0x1600 && idx < 0x1602;
    const bool tpdo_map = idx >= 0x1A00 && idx < 0x1A02;
    const bool rpdo_comm = idx >= 0x1400 && idx < 0x1402;
    const bool tpdo_comm = idx >= 0x1800 && idx < 0x1802;
    if (rpdo_map || tpdo_map) {
      const auto comm = static_cast<uint16_t>(idx - 0x200);
      const bool valid = (n.Value(comm, 1) & 0x80000000U) == 0;
      if (valid) {
        return 0x08000022; // present device state: invalidate the PDO first
      }
      if (o.sub >= 1 && n.Value(idx, 0) != 0) {
        return 0x08000022; // clear the count before changing an entry
      }
      if (o.sub == 0) {
        const uint32_t count = FromLe(data);
        uint32_t bits = 0;
        if (count > 8) {
          return 0x06040042;
        }
        for (uint32_t i = 1; i <= count; ++i) {
          const uint32_t rec = n.Value(idx, static_cast<uint8_t>(i));
          if ((rec >> 16) == 0 || (rec & 0xFF) == 0) {
            return 0x06040041;
          }
          bits += rec & 0xFF;
        }
        if (bits > 64) {
          return 0x06040042;
        }
      }
    }
    if ((rpdo_comm || tpdo_comm) && o.sub == 1) {
      const uint32_t old = n.Value(idx, 1);
      const uint32_t now = FromLe(data);
      if (((old & 0x80000000U) == 0) && ((now & 0x80000000U) == 0) && old != now) {
        return 0x08000022; // the identifier cannot change while the PDO is valid
      }
      if (((now & 0x80000000U) == 0) && n.Value(static_cast<uint16_t>(idx + 0x200), 0) == 0) {
        return 0x08000022; // validating an empty mapping
      }
    }
    if ((idx == 0x1010 || idx == 0x1011) && o.sub >= 1) {
      const uint32_t sig = FromLe(data);
      const uint32_t want = (idx == 0x1010) ? 0x65766173U : 0x64616F6CU;
      if (sig != want) {
        return 0x08000020;
      }
    }
    return 0;
  }

  void commit(SimNode& n, Object& o, const std::vector<uint8_t>& data) {
    o.value = data;
    const auto base = static_cast<uint16_t>(0x6040 + n.drive_offset);
    if (o.index == base && o.sub == 0) {
      n.drive.WriteControlword(static_cast<uint16_t>(FromLe(data)), now_us);
    } else if (o.index == static_cast<uint16_t>(base + 0x20) && o.sub == 0) {
      n.drive.WriteMode(static_cast<int8_t>(data[0]), now_us);
    } else if (o.index == 0x1017) {
      for (std::size_t k = 0; k < nodes.size(); ++k) {
        if (&nodes[k] == &n) {
          hb_next_[k] = 0;
        }
      }
    } else {
      // no side effect
    }
  }

  void sdoRequest(uint8_t node, const CanFrame& f) {
    SimNode* np = Node(node);
    if (np == nullptr || !np->responds || np->nmt == 4 || np->nmt == 0) {
      return;
    }
    SimNode& n = *np;
    ++n.sdo_requests;
    const auto cs = static_cast<uint8_t>(f.data[0] >> 5);
    const auto index = static_cast<uint16_t>(static_cast<uint32_t>(f.data[1]) |
                                             (static_cast<uint32_t>(f.data[2]) << 8U));
    const uint8_t sub = f.data[3];
    if (cs == 4) {
      ++n.aborts_received;
      n.last_abort_code =
          static_cast<uint32_t>(f.data[4]) | (static_cast<uint32_t>(f.data[5]) << 8) |
          (static_cast<uint32_t>(f.data[6]) << 16) | (static_cast<uint32_t>(f.data[7]) << 24);
      n.sdo = SimNode::Sdo::Idle;
      return;
    }
    if (n.abort_next_with != 0 && (cs == 1 || cs == 2)) {
      const uint32_t code = n.abort_next_with;
      n.abort_next_with = 0;
      sdoAbort(n, index, sub, code);
      return;
    }
    switch (cs) {
    case 2:
      initiateUpload(n, index, sub);
      break;
    case 3:
      uploadSegment(n, f);
      break;
    case 1:
      initiateDownload(n, f, index, sub);
      break;
    case 0:
      downloadSegment(n, f);
      break;
    default:
      sdoAbort(n, index, sub, 0x05040001);
      break;
    }
  }

  void initiateUpload(SimNode& n, uint16_t index, uint8_t sub) {
    Object* o = n.Find(index, sub);
    if (o == nullptr) {
      sdoAbort(n, index, sub, n.HasIndex(index) ? 0x06090011U : 0x06020000U);
      return;
    }
    if (!o->readable) {
      sdoAbort(n, index, sub, 0x06010001);
      return;
    }
    readHook(n, *o);
    n.sdo_index = index;
    n.sdo_sub = sub;
    CanFrame r = sdoFrame(n);
    r.data[1] = static_cast<uint8_t>(index & 0xFF);
    r.data[2] = static_cast<uint8_t>(index >> 8);
    r.data[3] = sub;
    if (o->value.size() <= 4 && !o->domain) {
      r.data[0] = static_cast<uint8_t>(0x43 | ((4 - o->value.size()) << 2));
      std::copy(o->value.begin(), o->value.end(), r.data.begin() + 4);
      n.sdo = SimNode::Sdo::Idle;
    } else {
      if (!n.supports_segmented) {
        sdoAbort(n, index, sub, 0x05040001);
        return;
      }
      r.data[0] = 0x41;
      const auto size = static_cast<uint32_t>(o->value.size());
      for (std::size_t i = 0; i < 4U; ++i) {
        r.data[4U + i] = static_cast<uint8_t>((size >> (8U * i)) & 0xFFU);
      }
      n.sdo = SimNode::Sdo::Up;
      n.sdo_toggle = false;
      n.sdo_buf = o->value;
      n.sdo_pos = 0;
    }
    Reply(r);
  }

  void uploadSegment(SimNode& n, const CanFrame& f) {
    if (n.sdo != SimNode::Sdo::Up) {
      sdoAbort(n, 0, 0, 0x05040001);
      return;
    }
    const bool t = (f.data[0] & 0x10) != 0;
    if (t != n.sdo_toggle) {
      sdoAbort(n, n.sdo_index, n.sdo_sub, 0x05030000);
      return;
    }
    const std::size_t left = n.sdo_buf.size() - n.sdo_pos;
    const std::size_t take = std::min<std::size_t>(left, 7);
    const bool last = take == left;
    CanFrame r = sdoFrame(n);
    r.data[0] =
        static_cast<uint8_t>((n.sdo_toggle ? 0x10U : 0U) | ((7U - take) << 1U) | (last ? 1U : 0U));
    std::copy_n(n.sdo_buf.begin() + static_cast<std::ptrdiff_t>(n.sdo_pos),
                static_cast<std::ptrdiff_t>(take), r.data.begin() + 1);
    n.sdo_pos += take;
    n.sdo_toggle = !n.sdo_toggle;
    if (last) {
      n.sdo = SimNode::Sdo::Idle;
    }
    Reply(r);
  }

  void initiateDownload(SimNode& n, const CanFrame& f, uint16_t index, uint8_t sub) {
    Object* o = n.Find(index, sub);
    if (o == nullptr) {
      sdoAbort(n, index, sub, n.HasIndex(index) ? 0x06090011U : 0x06020000U);
      return;
    }
    if (!o->writable) {
      sdoAbort(n, index, sub, 0x06010002);
      return;
    }
    const bool e = (f.data[0] & 2) != 0;
    const bool s = (f.data[0] & 1) != 0;
    CanFrame r = sdoFrame(n);
    r.data[0] = 0x60;
    r.data[1] = f.data[1];
    r.data[2] = f.data[2];
    r.data[3] = sub;
    if (e) {
      const std::size_t size = s ? 4U - ((static_cast<unsigned>(f.data[0]) >> 2U) & 3U) : 4U;
      std::vector<uint8_t> data(f.data.begin() + 4, f.data.begin() + 4 + size);
      if (!o->domain && size != o->value.size()) {
        sdoAbort(n, index, sub, size > o->value.size() ? 0x06070012U : 0x06070013U);
        return;
      }
      const uint32_t rule = writeRule(n, *o, data);
      if (rule != 0) {
        sdoAbort(n, index, sub, rule);
        return;
      }
      commit(n, *o, data);
      Reply(r);
      return;
    }
    const std::size_t size =
        static_cast<std::size_t>(f.data[4]) | (static_cast<std::size_t>(f.data[5]) << 8) |
        (static_cast<std::size_t>(f.data[6]) << 16) | (static_cast<std::size_t>(f.data[7]) << 24);
    if (!n.supports_segmented) {
      sdoAbort(n, index, sub, 0x05040001);
      return;
    }
    if (!s || (!o->domain && size != o->value.size()) || (o->domain && size > o->max_len)) {
      uint32_t code = 0x05040001U;
      if (s) {
        code = (size > o->value.size()) ? 0x06070012U : 0x06070013U;
      }
      sdoAbort(n, index, sub, code);
      return;
    }
    n.sdo = SimNode::Sdo::Down;
    n.sdo_toggle = false;
    n.sdo_buf.clear();
    n.sdo_index = index;
    n.sdo_sub = sub;
    n.sdo_expected = size;
    Reply(r);
  }

  void downloadSegment(SimNode& n, const CanFrame& f) {
    if (n.sdo != SimNode::Sdo::Down) {
      sdoAbort(n, 0, 0, 0x05040001);
      return;
    }
    const bool t = (f.data[0] & 0x10) != 0;
    if (t != n.sdo_toggle) {
      sdoAbort(n, n.sdo_index, n.sdo_sub, 0x05030000);
      return;
    }
    const std::size_t take = 7U - ((static_cast<unsigned>(f.data[0]) >> 1U) & 7U);
    const bool last = (f.data[0] & 1) != 0;
    n.sdo_buf.insert(n.sdo_buf.end(), f.data.begin() + 1, f.data.begin() + 1 + take);
    CanFrame r = sdoFrame(n);
    r.data[0] = static_cast<uint8_t>(0x20U | (n.sdo_toggle ? 0x10U : 0U));
    n.sdo_toggle = !n.sdo_toggle;
    if (last) {
      Object* o = n.Find(n.sdo_index, n.sdo_sub);
      if (n.sdo_buf.size() != n.sdo_expected || o == nullptr) {
        sdoAbort(n, n.sdo_index, n.sdo_sub, 0x06070010);
        return;
      }
      commit(n, *o, n.sdo_buf);
      n.sdo = SimNode::Sdo::Idle;
    }
    Reply(r);
  }
};

/// The full client port over a `SimBus`.
class MockPort : public canopen_client::CanInterface<MockPort> {
public:
  SimBus bus;

  bool EnsureInitialized() noexcept {
    ++bus.init_calls;
    return bus.init_ok;
  }
  bool Send(const CanFrame& frame) noexcept {
    const int index = bus.sends++;
    bus.last_sent_ptr = &frame;
    if (!bus.send_ok || index == bus.fail_send_index) {
      return false;
    }
    bus.FromClient(frame);
    return true;
  }
  RxStatus Receive(CanFrame& frame, uint32_t timeout_us) noexcept {
    const int index = bus.receives++;
    if (bus.receive_error || index == bus.fail_receive_index) {
      return RxStatus::Error;
    }
    const uint64_t deadline = bus.now_us + timeout_us;
    bus.PumpHeartbeats(deadline);
    if (!bus.rx.empty() && bus.rx.front().at_us <= deadline) {
      const Queued q = bus.rx.front();
      bus.rx.erase(bus.rx.begin());
      bus.now_us = std::max(bus.now_us, q.at_us);
      frame = q.frame;
      return RxStatus::Frame;
    }
    bus.now_us = deadline;
    return RxStatus::Timeout;
  }
  [[nodiscard]] uint32_t NowUs() const noexcept {
    return static_cast<uint32_t>(bus.now_us & 0xFFFF'FFFFU);
  }
};

/// `MockPort` plus the optional `Log()` hook.
class LoggingPort : public canopen_client::CanInterface<LoggingPort> {
public:
  MockPort inner;
  bool EnsureInitialized() noexcept {
    return inner.EnsureInitialized();
  }
  bool Send(const CanFrame& frame) noexcept {
    return inner.Send(frame);
  }
  RxStatus Receive(CanFrame& frame, uint32_t timeout_us) noexcept {
    return inner.Receive(frame, timeout_us);
  }
  [[nodiscard]] uint32_t NowUs() const noexcept {
    return inner.NowUs();
  }
  void Log(canopen_client::LogLevel level, const char* message) noexcept {
    static_cast<void>(level);
    inner.bus.logs.emplace_back(message);
  }
};

/// A transmit-only port: sends, never receives, has no clock.
class TxOnlyPort : public canopen_client::CanInterface<TxOnlyPort> {
public:
  std::vector<CanFrame> frames;
  bool init_ok = true;
  bool send_ok = true;
  int init_calls = 0;
  bool EnsureInitialized() noexcept {
    ++init_calls;
    return init_ok;
  }
  bool Send(const CanFrame& frame) noexcept {
    if (!send_ok) {
      return false;
    }
    frames.push_back(frame);
    return true;
  }
};

/// A frame from bytes, for tests.
inline CanFrame Frame(uint16_t id, std::vector<uint8_t> bytes, bool rtr = false) {
  CanFrame f;
  f.id = id;
  f.rtr = rtr;
  f.dlc = static_cast<uint8_t>(bytes.size());
  for (std::size_t i = 0; i < bytes.size() && i < 8; ++i) {
    f.data[i] = bytes[i];
  }
  return f;
}

} // namespace canopen_client_test

#endif // CANOPEN_CLIENT_MOCK_CANOPEN_CLIENT_HPP

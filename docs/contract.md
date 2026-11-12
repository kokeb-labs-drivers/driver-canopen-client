# Contract

What the CANopen client promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it.

## Promises

- The constructor is `noexcept`, stores the configuration, does no I/O and cannot fail.
- `EnsureInitialized()` validates the configuration (`InvalidArgument` before the port is touched), starts the port
  and clears the node table, the PDO subscriptions, the EMCY queue and the counters. Afterwards it returns `Ok` with
  no I/O.
- Every call that sends or receives returns `Error::NotInitialized` before `EnsureInitialized()` succeeds, and that
  wins over `InvalidArgument`. No call starts the port as a side effect.
- `InvalidArgument` means nothing was sent and nothing was read.
- Every port failure propagates as `Error::Bus`; a missing or silent node is `Timeout`; a node that refuses a
  transfer is `SdoAbort` with the node's code in `GetLastAbort()`.
- One request is in flight at a time and every call blocks until it ends or its timeout expires. After a timeout the
  driver sends an SDO abort (`Config::abort_on_timeout`) so the node's server is released.
- A reply is accepted only when its identifier is the node's, its command specifier fits the request and, for a
  segmented transfer, its toggle bit alternates. A lying node cannot overrun the caller's buffer, finish a transfer it
  has not completed, or keep a call running past its timeout.
- A segmented upload that does not fit the caller's buffer is aborted with 0x05040005 and returns `BufferTooSmall`;
  nothing is written past the buffer.
- A PDO configuration that fails part-way leaves the PDO invalid, never half configured and valid.
- `DriveDisable()` and `DriveQuickStop()` are never refused for the drive's state: they are sent up to
  `Config::safety_attempts` times. When `Receive()` fails the driver drops its receive state, sends the frame anyway and
  reports `Bus` afterwards. `SendNmt(Stop)` is a single attempt (NMT has no confirmation) and also sends past a failing
  receive side.
- A wait ends when its time budget is spent, counted only on frames the port returns; `Timeout` returns do not consume
  the frame budget. A drive wait is bounded by `Config::drive_timeout_us`, however small the poll period.
  `DriveResetFault()` spends one `drive_timeout_us` in all (the fault-reaction wait and the wait for the fault to
  clear share one clock). Under a flood of frames a pause between two polls ends after `kMaxFramesPerWait` frames, so
  the iteration is shorter than the poll interval and the step limit (`drive_timeout_us / drive_poll_interval_us + 2`)
  can end the wait before the timeout: always early (a `Timeout`), never late.
- `DriveResetFault()` writes 0x0000 (Disable voltage, the state the drive is in) after the fault has cleared, so
  controlword bit 7 is not left high.
- Node ages (`alive`, the Stopped gate) are compared in 32 bits of `NowUs()`, which wraps at 71.6 minutes: a
  heartbeat older than that can look fresh. Call `GetNodeStatus()` or `Poll()` at least once an hour, or treat a node
  with no recent `count` change as lost. The waits (SDO, guard, drive) are wrap-safe.
- Frames that arrive while a drive wait pauses between polls are routed (heartbeats, EMCY, TPDOs), not lost.
- `ReadRawSdo()`, `WriteRawSdo()`, `SendRawFrame()` and `ReadRawFrame()` are single-shot diagnostic access: no retry,
  no validation beyond framing. The raw frame returned is a copy owned by the driver (`GetLastRawFrame()`). They
  **bypass the safety and state guards**: no node-state gate, and any frame can go out, including an NMT command, an
  RPDO or a drive controlword. They do not update the node table (a raw NMT Stop leaves the table saying
  Operational), so stop and disable must use `SendNmt()`, `DriveDisable()` and `DriveQuickStop()`. `ReadRawSdo()` and
  `WriteRawSdo()` drain the receive queue first (frames are routed as usual), so the frame they return is the first
  one that arrives after their request; `ReadRawFrame()` returns whatever is next, including an older frame.
- The `Send` hook gets a frame the driver owns, valid only during the call: copy it to queue it. (The driver keeps it
  in a member that the next send overwrites.)
- A node whose heartbeat says it is Stopped is refused SDO calls with `NodeState` and no frame is sent.

## Ambiguities and how the driver reads them

- **Exact-size reads.** The typed reads (`SdoReadU8()`, `SdoReadU16()`, `SdoReadU32()`) require the node's reply to be
  exactly the size asked for; any other size is `Protocol` and the value is not used.
- **Stale replies.** CANopen has no request identifier: a late reply to a request that timed out can be taken for the
  reply to the next request on the same node and index. The driver checks the identifier, the command specifier, the
  toggle bit and (for typed reads) the size, but cannot rule this out; `abort_on_timeout` keeps the window short.
- **Boot-up.** A node's boot-up message sets `bootup_seen` for that node. It stays set until `ClearBootUp(node)` or
  `EnsureInitialized()`, so an application can tell a restart from a long silence.
- **Bus-off.** After a bus-off the port recovers the controller; the application then calls `Deinitialize()` and
  `EnsureInitialized()` so the node table and the receive state start clean.
- **Guarding.** A guard reply whose toggle bit repeats returns `Protocol`, and the driver resynchronises to the
  toggle it saw, so the next reply is judged against the node's. A boot-up message (state 0) that arrives while a
  guard request is out is recorded (`bootup_seen`) and is not taken for the reply; the wait goes on.

## What is not verified

- **Hardware.** The driver has been run only against a model of the bus and its nodes. Treat every timing as **not
  verified on hardware** until the bench steps on the [Troubleshooting](troubleshooting.md) page have been run on a
  real node.
- **The specifications.** CiA 301 and CiA 402 are published behind a registration or a purchase and were not
  obtained. The byte layouts, the order of operations and the statusword masks come from public sources named in
  [`docs/datasheets.yml`](datasheets.yml) (an encyclopaedia article, a vendor tutorial, the open-source CANopenNode
  and python-canopen projects). Where a public source and the specification could differ, the driver does the
  stricter thing, and a node that does something unusual may be refused.
- **Block transfer, CAN FD, extended identifiers, LSS, NMT slave and SDO server** are not implemented. An
  extended-identifier, CAN FD or error frame must be dropped by the port before `Receive()` returns.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `canopen_client_version.h` |
| Your port | | A CAN controller: send, receive with a timeout, a microsecond clock |

## Threading

Not thread-safe; takes no lock. One owner task, or one mutex around every call. Nothing may run in an ISR: a call
waits inside the port's `Receive()`.

## Errors

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | Before `EnsureInitialized()` succeeded, or after `Deinitialize()` |
| `InvalidArgument` | 2 | An argument or a `Config` field is out of range; nothing was sent |
| `Bus` | 3 | The port failed to send or to receive |
| `Timeout` | 4 | No valid reply in time: SDO, node guarding or a drive state change |
| `DeviceFault` | 8 | A read-back (`Verify::ReadBack`) showed a value other than the one written |
| `Unsupported` | 9 | `ReadPdoConfig()` met a 29-bit PDO identifier |
| `SdoAbort` | 16 | The node aborted the SDO transfer; the code is in `GetLastAbort()` |
| `Protocol` | 17 | A reply broke the protocol: command specifier, toggle bit or size |
| `BufferTooSmall` | 18 | An upload is larger than the caller's buffer, or a fixed table is full |
| `InvalidFrame` | 19 | A frame fails validation; returned by the codecs, counted by the client |
| `NodeState` | 20 | The node's NMT state ignores the request |
| `DriveFault` | 21 | The drive is in Fault, or in a state no transition leaves |

`DeviceIdMismatch`, `Crc` and `Busy` are never returned: the CAN controller checks the frame CRC, and calls block.

## Memory

No heap, exceptions, RTTI or static mutable state. The object holds a port reference, a `Config`, the node table
(16), the TPDO subscriptions (16), the EMCY queue (8) and one transfer state; its size is fixed at compile time.

## Platform

Platform-free; the ESP-IDF port in `examples/esp32` is an example.

## Deviations from the standard

None.

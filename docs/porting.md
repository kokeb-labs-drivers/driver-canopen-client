# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O:

    - Supply voltage and mode select (RS): 8.3 Recommended Operating Conditions; 5 Description (continued), [p. 6](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=6)
    - CAN node: transceiver and controller wiring: 11.2 Typical Application, [p. 26](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=26)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver calls hooks on a class you derive from `canopen_client::CanInterface<Self>` (in
[`inc/canopen_client_can_interface.hpp`](../inc/canopen_client_can_interface.hpp)). The base declares no hook; the
driver template is constrained by the `canopen_client::CanPort` concept (and `CanRxPort` for the calls that need a
reply), so a missing or misdeclared hook is a compile error.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the controller up at the bus bit rate on the first call; `true` at once afterwards |
| `bool Send(const CanFrame&) noexcept` | yes | Queue one 11-bit frame; `false` when the controller refused it |
| `RxStatus Receive(CanFrame&, uint32_t timeout_us) noexcept` | for replies | Wait up to `timeout_us` for one frame (0 only looks): `Frame`, `Timeout` or `Error` |
| `uint32_t NowUs() noexcept` | with `Receive` | A free-running microsecond clock; every timeout is measured with it |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |

`Receive` and `NowUs` come as a pair. The port drops extended-identifier, CAN FD and error frames itself: the driver
only ever sees a classic frame. Report a bus-off or stopped controller as `RxStatus::Error`, and a quiet bus as
`Timeout`; they are different failures (`Bus` and `Timeout`).

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

A port with every hook:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

A node that only commands needs two hooks, and the calls that need a reply do not compile:

```cpp
--8<-- "tests/docs/ports.cpp:tx-port"
```

The ESP32 example uses [`esp32_canopen_client_port.hpp`](../examples/esp32/main/esp32_canopen_client_port.hpp) (the
TWAI driver at 500 kbit/s).

## Bit rate and filters

The driver does not set the bit rate: `EnsureInitialized()` of the port does. Accept all 11-bit identifiers in the
controller's filter (or at least 0x080 to 0x7FF); heartbeats, EMCY and the SDO replies all arrive on different
identifiers, and a filter that drops one makes the matching call time out.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex.

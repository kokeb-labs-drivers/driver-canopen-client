# Quickstart

Read a node's device type over CAN and start it: wire a transceiver, write a small port class, call three driver
functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - CAN node: transceiver and controller wiring: Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O, 11.2 Typical Application, [p. 26](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=26)
    - NMT commands and states: christiansandberg python-canopen nmt.py master as downloaded, NMT commands and state codes, p. 1

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## 1. Wire it

| MCU | Transceiver | Notes |
| --- | --- | --- |
| CAN TX | D (TXD) | The controller's transmit output |
| CAN RX | R (RXD) | The controller's receive input |
| 3.3 V | VCC | 100 nF to GND close to the pin |
| GND | GND, RS | RS to GND selects high speed on the SN65HVD230 |

CANH and CANL go to the bus; the two ends of the bus carry a 120 Ω termination each ([Hardware](hardware.md)). Every
node on the bus must run the same bit rate; nothing here sets it, the port does.

## 2. Add the driver

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-canopen-client.git external/driver-canopen-client
```

## 3. Write the port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

## 4. Talk to a node

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

1. `EnsureInitialized()` starts the port once; nothing else starts it.
2. `ReadDeviceType()` reads object 0x1000 from node 5 with an SDO upload and waits for the reply.
3. `SendNmt(Start, 5)` moves the node to Operational. NMT has no reply: the node's next heartbeat shows the result.

No hardware yet? [`examples/host`](../examples/host/README.md) runs the same calls against a simulated bus.

Next: [API guide](api-guide.md).

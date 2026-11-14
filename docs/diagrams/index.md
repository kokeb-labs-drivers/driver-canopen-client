# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the SN65HVD230

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the SN65HVD230: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## What the SN65HVD230 does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the SN65HVD230: Start and stop nodes (supported): Sends the NMT commands that
    start, stop, pre-operate and reset one node or all of them; Read and write objects (supported):
    Reads and writes object dictionary entries, in one frame or in segments, with the node's abort code;
    Heartbeat, guarding and emergencies (supported): Watches node heartbeats and guarding replies, and
    queues the emergency messages nodes send; SYNC producer (supported): Sends the SYNC frame, once or
    on a period with the optional counter byte; Process data (supported): Maps and enables a node's PDOs
    in the order the profile asks, reads TPDO samples and sends RPDOs; CiA 402 drive control
    (supported): Reads the drive state and walks it to enabled, disabled, quick stop or fault reset, and
    sets the mode; Node identity and settings (supported): Reads a node's device type and error
    register, sets its heartbeat time, stores or restores its parameters; Raw frames (supported): Sends
    and reads any classic CAN frame and returns the raw reply of an SDO request; Block transfer, FD and
    LSS (not covered): Block transfer, CAN FD, 29-bit identifiers, LSS, an NMT slave and an SDO server
    are not implemented.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## SN65HVD230: A CAN node

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the SN65HVD230 a can node: the chip with U2 MCU with CAN controller, C1 capacitor 100
    nF, R1 resistor 120 Ω, J1 CAN bus; the MCU wires carry the port hooks Receive(CanFrame&,
    timeout_us), Send(const CanFrame&).">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## SN65HVD230

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the SN65HVD230, a 3.3 V CAN transceiver, the reference physical layer of a CANopen node:
    it turns a CAN controller's TXD and RXD into the differential CANH and CANL signals of an ISO
    11898-2 bus, and this driver speaks CANopen to the nodes on that bus. A MCU talks to it over CAN
    controller TXD and RXD; it drives CAN bus pair; it reads back Bus level. Headline figures: 3.0 to
    3.6 V single supply; 1 Mbit/s data rate, up to; 120 nodes on one bus; 16 kV bus pin ESD, HBM; 370 µA
    standby current, typical.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## SN65HVD230 package, SOIC-8

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the SN65HVD230 in SOIC-8 drawn to scale from above: body 4.9 x 3.9 x 1.75 mm, pitch 1.27
    mm, 8 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## SN65HVD230 pinout, SOIC-8

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the SN65HVD230 in SOIC-8, top view: 8 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## Wiring: A CAN node

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the SN65HVD230 for the A CAN node: MCU with CAN controller CAN TX to D (Send(const
    CanFrame&)); MCU with CAN controller CAN RX to R (Receive(CanFrame&, timeout_us)); CANH to CAN bus
    CANH; CANL to CAN bus CANL. Supplies, grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the Client core, which sends CAN frames through the CAN port
    contract to the port class you write, which drives the CAN controller; the core and the contract are
    the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/canopen_client.hpp`](../../inc/canopen_client.hpp)
- `CanInterface` in [`inc/canopen_client_can_interface.hpp`](../../inc/canopen_client_can_interface.hpp)

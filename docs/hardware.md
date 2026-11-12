# Hardware

What a CAN node needs: a controller, a transceiver, a terminated bus. The driver sees only the port's `Send()` and
`Receive()`; this page shows the reference wiring with the TI SN65HVD230 3.3 V transceiver, the one part whose
datasheet was obtained for this repository.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Pin configuration: Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O, 7 Pin Configuration and Functions, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5)
    - Supply voltage and mode select (RS): Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O, 8.3 Recommended Operating Conditions; 5 Description (continued), [p. 6](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=6)
    - CAN node: transceiver and controller wiring: Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O, 11.2 Typical Application, [p. 26](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=26)
    - Bus termination: Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O, 11.2.1.1 CAN Termination, [p. 26](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=26)
    - Pre-defined connection set (COB-IDs): Wikipedia contributors Wikipedia article (oldid 1373604442) last edited September 2026, Pre-defined connection set, p. 1

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The CANopen specifications themselves (CiA 301 and CiA 402) were not obtained; see
[Contract](contract.md#what-is-not-verified). The driver does not depend on the transceiver: any ISO 11898-2 part
with the same four signals works.

## Pins

<!-- generated: chip-pins -->
SOIC-8, 8 pins (7 Pin Configuration and Functions, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5)).

| Pin | Name | Type (domain) | Function | Driver hook | If unused | Datasheet |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `D` | digital in (VCC) | CAN transmit data (TXD): low is dominant, high is recessive | `Send(const CanFrame&)` | - | 7, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5) |
| 2 | `GND` | ground | Ground | - | - | 7, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5) |
| 3 | `VCC` | power (VCC) | Transceiver 3.3 V supply | - | - | 8.3, [p. 6](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=6) |
| 4 | `R` | digital out (VCC) | CAN receive data (RXD): low is dominant, high is recessive | `Receive(CanFrame&, timeout_us)` | - | 7, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5) |
| 5 | `Vref` | analog (VCC) | VCC / 2 reference output | - | Leave open | 10.3.1, [p. 20](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=20) |
| 6 | `CANL` | bus (VCC) | Low-level CAN bus line | - | - | 7, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5) |
| 7 | `CANH` | bus (VCC) | High-level CAN bus line | - | - | 7, [p. 5](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=5) |
| 8 | `RS` | digital in (VCC) | Mode select: pull-down to GND is high speed, pull-up to VCC is low power, 10 kΩ to 100 kΩ is slope control | - | Tie to GND for high speed | 10.4, [p. 20](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=20) |

Source of this table and of the chip figures: [`docs/hardware.yml`](hardware.yml).
<!-- /generated -->

The driver reaches the bus through two hooks. `Send()` hands a frame to the controller, which drives D. `Receive()`
waits for a frame the controller took from R. Pin levels, bit timing, arbitration and the frame CRC stay in the
controller.

## Supply and mode

| Parameter | Value | Where |
| --- | --- | --- |
| Supply, VCC | 3.0 V to 3.6 V | Recommended operating conditions |
| RS to GND | High-speed mode | Device functional modes |
| RS to VCC | Low-power standby | Device functional modes |
| RS through 10 kΩ to 100 kΩ to GND | Slope control (slower edges) | Device functional modes |

The port should leave RS at the level that gives the mode you want before the first `Send()`; a transceiver in
standby does not drive the bus, and the driver cannot tell that from a missing node (`Timeout`).

## Bus termination

The bus is a twisted pair of about 120 Ω, terminated with 120 Ω at each of its two ends and nowhere else. A bus
with no or one terminator can work at short range and then fail in ways that look like random `Timeout` results
([Troubleshooting](troubleshooting.md)). 60 Ω between CANH and CANL on a powered-off bus shows both terminators.

## Figures

The [pinout, package outline, block diagram and the reference circuit](diagrams/index.md) are drawn from
[`hardware.yml`](hardware.yml).

## Reference documents

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: SN65HVD23x 3.3-V CAN Bus Transceivers (Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346)) | O, 2018-04 | [PDF](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf) · [vendor page](https://www.ti.com/product/SN65HVD230) |
| Reference: CANopen (Wikipedia contributors Wikipedia article (oldid 1373604442)) | last edited September 2026, 2026-09 | [vendor page](https://en.wikipedia.org/wiki/CANopen) |
| Reference: CANopen Explained - A Simple Intro (CSS Electronics canopen-tutorial-simple-intro) | 2026 edition, 2026 | [vendor page](https://www.csselectronics.com/pages/canopen-tutorial-simple-intro) |
| Source: CANopenNode 301/CO_SDOserver.h (CANopenNode project CO_SDOserver.h) | master as downloaded, 2026-11 | [vendor page](https://github.com/CANopenNode/CANopenNode/blob/master/301/CO_SDOserver.h) |
| Source: CANopenNode 301/CO_Emergency.h (CANopenNode project CO_Emergency.h) | master as downloaded, 2026-11 | [vendor page](https://github.com/CANopenNode/CANopenNode/blob/master/301/CO_Emergency.h) |
| Source: CANopenNode 301/CO_HBconsumer.c (CANopenNode project CO_HBconsumer.c) | master as downloaded, 2026-11 | [vendor page](https://github.com/CANopenNode/CANopenNode/blob/master/301/CO_HBconsumer.c) |
| Source: CANopenNode 301/CO_SYNC.h (CANopenNode project CO_SYNC.h) | master as downloaded, 2026-11 | [vendor page](https://github.com/CANopenNode/CANopenNode/blob/master/301/CO_SYNC.h) |
| Source: CANopenNode storage/CO_storage.c (CANopenNode project CO_storage.c) | master as downloaded, 2026-11 | [vendor page](https://github.com/CANopenNode/CANopenNode/blob/master/storage/CO_storage.c) |
| Source: canopen/sdo/client.py (christiansandberg python-canopen sdo/client.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/sdo/client.py) |
| Source: canopen/sdo/constants.py (christiansandberg python-canopen sdo/constants.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/sdo/constants.py) |
| Source: canopen/sdo/base.py (christiansandberg python-canopen sdo/base.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/sdo/base.py) |
| Source: canopen/nmt.py (christiansandberg python-canopen nmt.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/nmt.py) |
| Source: canopen/emcy.py (christiansandberg python-canopen emcy.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/emcy.py) |
| Source: canopen/network.py (christiansandberg python-canopen network.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/network.py) |
| Source: canopen/pdo/base.py (christiansandberg python-canopen pdo/base.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/pdo/base.py) |
| Source: canopen/profiles/p402.py (christiansandberg python-canopen profiles/p402.py) | master as downloaded, 2026-11 | [vendor page](https://github.com/christiansandberg/canopen/blob/master/canopen/profiles/p402.py) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

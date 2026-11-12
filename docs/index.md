---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# CANOPEN_CLIENT Driver

<p>A generic C++20 CANopen client over a classic CAN frame port: NMT master, SDO client, heartbeat, emergency,
SYNC, PDOs and a CiA 402 drive state machine.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-canopen-client){ .md-button }
</div>

The driver is not tied to a product or to a CAN controller. You give it a small class that sends one 11-bit frame,
waits for one and reads a microsecond clock; it does the rest: the SDO transfer rules, heartbeat ageing, the
emergency queue, PDO configuration in the order the profile prescribes and the drive's state transitions.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">CAN node</span>
<span class="kb-chip__name">CANopen client</span>
<span class="kb-chip__vendor">Any CANopen node</span>
</div>

Bus
:   Classic CAN, 11-bit identifiers, any bit rate your controller runs

Services
:   NMT, SDO (expedited and segmented), heartbeat, node guarding, EMCY, SYNC, TPDO and RPDO

Profiles
:   CANopen application layer; CiA 402 drive state machine

Reference transceiver
:   TI SN65HVD230, 3.0 V to 3.6 V supply (see [Hardware](hardware.md))
</div>

## What you get

- **One object per bus**, no I/O until `EnsureInitialized()`. See [API guide](api-guide.md).
- **Errors you can act on**: `Result<T>` with an `Error`, and the node's own SDO abort code. See
  [Contract](contract.md).
- **Zero-overhead portability**: a CRTP port checked by a concept; a send-only port gets a send-only API. See
  [Porting](porting.md).
- **Tested without hardware** against an independent model of the bus and its nodes. See [Testing and CI](testing.md).

## Datasheet

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

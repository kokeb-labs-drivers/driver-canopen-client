# Troubleshooting

Symptoms, causes and fixes, cheapest check first.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Bus termination: Texas Instruments SN65HVD230, SN65HVD231, SN65HVD232 (SLOS346) O, 11.2.1.1 CAN Termination, [p. 26](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf#page=26)
    - SDO server behaviour: toggle bit, size, abort: CANopenNode project CO_SDOserver.h master as downloaded, CO_SDOserver.h: transfer sequence and abort codes, p. 1
    - Heartbeat consumer: CANopenNode project CO_HBconsumer.c master as downloaded, CO_HBconsumer.c, p. 1
    - SDO command specifiers and abort codes: christiansandberg python-canopen sdo/constants.py master as downloaded, SDO constants, p. 1
    - Node guarding toggle bit: christiansandberg python-canopen nmt.py master as downloaded, NMT guarding reply, p. 1
    - CiA 402 state machine and controlword: christiansandberg python-canopen profiles/p402.py master as downloaded, BaseNode402: states, masks, transitions, p. 1

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `EnsureInitialized()` returns `Bus` | The port could not start the controller | Check the pins, the bit rate and that no other code owns the peripheral |
| `EnsureInitialized()` returns `InvalidArgument` | A `Config` field is out of range | Check it with `canopen_client::Validate()` ([Configuration](configuration.md)) |
| Every SDO call returns `Timeout` | Wrong bit rate or node id, no termination, transceiver in standby, node not powered | Check the bit rate on every node, 60 Ω between CANH and CANL ([Hardware](hardware.md)), RS at ground |
| Some calls time out, others work | A receive filter drops an identifier, or a node is slow | Accept 0x080 to 0x7FF; use `SlowBusPreset()` |
| `SdoAbort` | The node refused the object | `GetLastAbort()`: 0x06020000 object missing, 0x06010002 write to a read-only object, 0x06090011 sub-index missing |
| `NodeState` with no frame sent | The node's heartbeat says it is Stopped | `SendNmt(Start, node)` or `SendNmt(PreOperational, node)` |
| `Protocol` | The node's reply broke the rules, or a short SDO frame with strict length | Try `strict_sdo_length = false` if the node sends short frames |
| `BufferTooSmall` from an upload | The object is larger than your buffer | Pass a bigger buffer; the transfer was aborted with 0x05040005 |
| `DriveEnable()` returns `Timeout` | The drive did not reach Operation enabled in `drive_timeout_us` | Raise it (the wait is bounded by this time, not by a step count); read `ReadStatusword()` for the state the drive stopped in |
| `DriveEnable()` returns `DriveFault` | The drive is in Fault | Fix the cause, then `DriveResetFault()` |
| A node is never `alive` | Its heartbeat producer time is 0, or it is not registered | `WriteHeartbeatProducerTime()`, then `MonitorNode()` with a timeout above the period |
| `Stats::emcy_dropped` grows | The EMCY queue (8) is not drained | Call `PopEmcy()` after every `Poll()` |

## Bench steps

None of the driver's timing has run on hardware. To confirm it on a node: read 0x1000 with `ReadDeviceType()` and
compare it with the node's documentation; start the node and watch `GetNodeStatus()` follow its heartbeat; write a
read-only object and check `SdoAbort` with 0x06010002 comes back; unplug the node and check `Timeout` within
`sdo_timeout_us`. Record the result in the pull request.

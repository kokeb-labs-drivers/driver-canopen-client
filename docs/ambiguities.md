# Ambiguities

What the driver's sources leave open, and how the driver deals with it.

## Secondary sources until CiA 301 and CiA 402 are obtained

The driver follows CANopen, but the CAN in Automation specifications that define it, CiA 301 (the application layer
and communication profile) and CiA 402 (the drive profile), were not obtained. Every protocol fact in the driver and
in these pages comes from secondary sources instead: the open-source CANopenNode stack, the Python `canopen` package,
a CANopen tutorial and an encyclopaedia article ([Datasheet map](datasheet.md) lists each one and what it is used
for). Only the transceiver facts come from a vendor datasheet.

| Where it matters | What a secondary source cannot settle | What the driver does |
| --- | --- | --- |
| Frame layouts, COB-IDs, the node ID range 1 to 127, object indices, SDO abort codes | Whether the sources follow the specification | Uses the values the sources give. The host tests run the driver against a node model written from the same sources, so they cannot catch an error the sources share |
| CiA 402 state machine and modes of operation | Which transitions a drive must accept and which modes it must implement | Walks the states the sources describe; disable and quick stop are never refused for the drive state |
| Timing (the SDO reply, the guarding reply, a drive state change) | The specification's limits | Each wait is a `Config` value with a default and a checked range ([Configuration](configuration.md)) |
| Bit timing (the bit rates and sample point a CANopen network uses) | The specification's bit-timing table | No bit-timing figure: the controller's bit timing is the port's. [On the bus](bus.md) draws only what the SN65HVD230 datasheet gives (the loop delays, the bus levels, the termination) and marks the node ID range as taken from a secondary source |

Until the specifications are obtained, treat the protocol behaviour as verified against the sources and the host
tests only, not against CiA 301 and CiA 402. When they are, check each row of the [Datasheet map](datasheet.md)
against them and remove this section.

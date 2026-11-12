# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Fixed

- Drive waits are pinned to `drive_timeout_us` by tests on a slow node; `DriveResetFault()` uses one clock for both
  waits and leaves the controlword at 0x0000; `ReadRawSdo()`/`WriteRawSdo()` drain stale replies first; a boot-up
  frame during a guard request is no longer taken as the guard reply.

### Added

- CANopen client driver, first version: NMT master, SDO client (expedited and segmented, with the node's abort
  codes), heartbeat and node-guarding consumer, EMCY queue, SYNC producer, TPDO and RPDO with PDO configuration in
  the profile's order, store and restore parameters, and CiA 402 drive helpers, over a classic CAN frame port
  (`CanInterface`: `Send`, `Receive`, `NowUs`). Not verified on hardware.
- Host example, ESP32 TWAI example, and tests against an independent model of the bus, its nodes and a drive.

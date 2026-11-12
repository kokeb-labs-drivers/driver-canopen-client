# API guide

The driver's API by task, with code that compiles.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - NMT master frame (identifier 0, command then node): Wikipedia contributors Wikipedia article (oldid 1373604442) last edited September 2026, Network management (NMT), p. 1
    - Object dictionary and communication objects: CSS Electronics canopen-tutorial-simple-intro 2026 edition, Object dictionary, p. 1
    - Emergency message layout: CANopenNode project CO_Emergency.h master as downloaded, CO_Emergency.h: EMCY frame, p. 1
    - Heartbeat consumer: CANopenNode project CO_HBconsumer.c master as downloaded, CO_HBconsumer.c, p. 1
    - SYNC producer and counter: CANopenNode project CO_SYNC.h master as downloaded, CO_SYNC.h: SYNC frame and counter, p. 1
    - Store and restore parameters: CANopenNode project CO_storage.c master as downloaded, CO_storage.c: signatures and sub-indices, p. 1
    - SDO expedited and segmented transfer: christiansandberg python-canopen sdo/client.py master as downloaded, SdoClient: initiate, upload, download, segments, p. 1
    - SDO command specifiers and abort codes: christiansandberg python-canopen sdo/constants.py master as downloaded, SDO constants, p. 1
    - SDO object access and data types: christiansandberg python-canopen sdo/base.py master as downloaded, SdoBase: variable access, p. 1
    - NMT commands and states: christiansandberg python-canopen nmt.py master as downloaded, NMT commands and state codes, p. 1
    - Node guarding toggle bit: christiansandberg python-canopen nmt.py master as downloaded, NMT guarding reply, p. 1
    - Emergency message consumer: christiansandberg python-canopen emcy.py master as downloaded, EmcyConsumer, p. 1
    - PDO communication and mapping parameters: christiansandberg python-canopen pdo/base.py master as downloaded, PDO map and communication records, p. 1
    - CiA 402 state machine and controlword: christiansandberg python-canopen profiles/p402.py master as downloaded, BaseNode402: states, masks, transitions, p. 1
    - CiA 402 modes of operation: christiansandberg python-canopen profiles/p402.py master as downloaded, Operation modes, p. 1

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

| Group | Calls |
| --- | --- |
| Lifecycle | `EnsureInitialized()`, `IsInitialized()`, `Deinitialize()`, `SetConfig()`, `GetConfig()`, `canopen_client::Validate()` |
| NMT and SYNC (send only) | `SendNmt()`, `SendSync()`, `SendRpdo()`, `SdoDownloadUnconfirmed()` |
| SDO | `SdoUpload()`, `SdoDownload()`, `SdoReadU8/16/32()`, `SdoWriteU8/16/32()`, `GetLastAbort()` |
| Standard objects | `ReadDeviceType()`, `ReadErrorRegister()`, `WriteHeartbeatProducerTime()`, `StoreParameters()`, `RestoreDefaults()` |
| Receive path | `Poll()`, `MonitorNode()`, `UnmonitorNode()`, `GetNodeStatus()`, `GuardNode()`, `PopEmcy()`, `GetStats()` |
| SYNC producer | `ConfigureSync()`, `ServiceSync()` |
| PDO | `ConfigurePdo()`, `DisablePdo()`, `ReadPdoConfig()`, `SubscribeTpdo()`, `UnsubscribeTpdo()`, `ReadTpdo()` |
| CiA 402 drive | `ReadStatusword()`, `ReadDriveState()`, `DriveEnable()`, `DriveDisable()`, `DriveQuickStop()`, `DriveResetFault()`, `SetDriveMode()`, `ReadDriveMode()` |

A call that needs a reply exists only on a port with `Receive()` and `NowUs()`; the commands in the second row work on
a send-only port ([Porting](porting.md)). Every call returns `Result<T>`: check `ok()` or `operator bool`, then read
`.value` or `.error`.

## Read and write objects (SDO)

```cpp
--8<-- "tests/docs/api_tasks.cpp:sdo"
```

Sizes of 1 to 4 bytes travel as one expedited exchange, larger ones as a segmented transfer; the driver picks.
After `SdoAbort`, `GetLastAbort()` holds the node, object and the 32-bit abort code, and
`canopen_client::AbortCodeName()` names the common ones.

For diagnosis there is a raw, single-shot path: `ReadRawSdo()` and `WriteRawSdo()` send one request and return the
node's reply frame as it is; `SendRawFrame()` and `ReadRawFrame()` put one frame on the bus or take one from it;
`GetLastRawFrame()` holds the last frame the driver saw. None of them retries or checks more than the framing. They
bypass the safety and state guards and do not update the node table, so use `SendNmt()`, `DriveDisable()` and
`DriveQuickStop()` to stop a node. A
boot-up message leaves `bootup_seen` set for its node until `ClearBootUp(node)`.

## Watch nodes and read emergencies

```cpp
--8<-- "tests/docs/api_tasks.cpp:monitor"
```

`Poll()` is the receive loop: call it from your task with the longest wait you can afford. It routes one frame to
the node table, the EMCY queue, a TPDO sample or the SYNC counter and returns which. Heartbeats are consumed only for
nodes registered with `MonitorNode()`; `GuardNode()` is the polled alternative for nodes that answer node guarding.

## Process data

```cpp
--8<-- "tests/docs/api_tasks.cpp:pdo"
```

`ConfigurePdo()` writes the node's communication and mapping records in the profile's order, so the PDO is never valid
while half written. Configure a node in Pre-operational and start it with `SendNmt(Start, node)`; a node sends its
TPDOs only when Operational.

## Drive a CiA 402 node

```cpp
--8<-- "tests/docs/api_tasks.cpp:drive"
```

`DriveEnable()` walks the state machine one transition at a time, writing the next controlword and reading the
statusword until the drive reports Operation enabled or `Config::drive_timeout_us` of bus time passes, however small
the poll period. A drive that maps its objects elsewhere passes a `DriveObjects` with its indices.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

Next: [Configuration](configuration.md).

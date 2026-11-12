# Configuration

Every field of `canopen_client::Config`, the presets, and the optional Kconfig layer.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From CANopenNode project CO_SDOserver.h master as downloaded:

    - SDO server behaviour: toggle bit, size, abort: CO_SDOserver.h: transfer sequence and abort codes, p. 1

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

| Field | Default | Range |
| --- | --- | --- |
| `sdo_timeout_us` | 200000 | 1 ms to 60 s; the wait for each SDO reply |
| `guard_timeout_us` | 100000 | 1 ms to 10 s; the wait for a node-guarding reply |
| `drive_timeout_us` | 2000000 | 1 ms to 60 s; the wait for a drive state change, in bus time |
| `drive_poll_interval_us` | 10000 | 100 us to 1 s; the pause between statusword reads |
| `safety_attempts` | 3 | 1 to 8; sends of a stop or disable command before it gives up |
| `abort_on_timeout` | true | Send an SDO abort (0x05040000) to the node after a timeout |
| `strict_sdo_length` | true | Require every SDO frame to carry 8 data bytes |

`Config` is stored by the constructor and checked by `EnsureInitialized()` and `SetConfig()`; `canopen_client::Validate()`
checks one without touching the bus. `SetConfig()` changes the timeouts of a running client and refuses a bad value
without changing anything. A node that sends short SDO frames (some do) needs `strict_sdo_length = false`.

| Preset | What it sets |
| --- | --- |
| `FastBusPreset()` | SDO 50 ms, guarding 20 ms, drive poll every 2 ms: a fast, lightly loaded bus |
| `SlowBusPreset()` | SDO 1 s, guarding 500 ms, drive 5 s, poll every 50 ms: 125 kbit/s with many nodes |

```cpp
--8<-- "tests/docs/configuration.cpp:preset"
```

## Kconfig

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_CANOPEN_CLIENT_SDO_TIMEOUT_MS` | 200 | `sdo_timeout_us` |
| `CONFIG_CANOPEN_CLIENT_GUARD_TIMEOUT_MS` | 100 | `guard_timeout_us` |
| `CONFIG_CANOPEN_CLIENT_DRIVE_TIMEOUT_MS` | 2000 | `drive_timeout_us` |
| `CONFIG_CANOPEN_CLIENT_DRIVE_POLL_INTERVAL_MS` | 10 | `drive_poll_interval_us` |
| `CONFIG_CANOPEN_CLIENT_SAFETY_ATTEMPTS` | 3 | `safety_attempts` |
| `CONFIG_CANOPEN_CLIENT_ABORT_ON_TIMEOUT` | y | `abort_on_timeout` |
| `CONFIG_CANOPEN_CLIENT_STRICT_SDO_LENGTH` | y | `strict_sdo_length` |

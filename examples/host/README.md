# CANOPEN_CLIENT host example

The driver on your PC, with no hardware: the host port in
[`host_canopen_client_port.hpp`](host_canopen_client_port.hpp) connects the real driver to a simulated CAN bus
(the node model the host tests use), and [`main.cpp`](main.cpp) starts a drive node, configures a PDO, enables the
drive and reads an emergency message.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/canopen_client_host_example
```

## Expected output

```text
canopen_client driver 0.1.0 on the host
node 5: device type 0x00020192
TPDO 0x185: CD AB 5A
drive: OperationEnabled
EMCY from node 5: code 0x2310
```

The host tests build this folder too and run the example (CTest `canopen_client_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: start, configure, enable, read |
| [`host_canopen_client_port.hpp`](host_canopen_client_port.hpp) | The host port: `Send`, `Receive`, `NowUs` over the simulated bus, and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |

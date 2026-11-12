# CANopen client ESP32 examples

ESP-IDF applications that run the CANopen client driver on an ESP32-C6 over the on-chip TWAI controller, built by CI on
every pull request. Not verified on hardware.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | CAN transceiver | Notes |
| --- | --- | --- |
| GPIO4 | TXD | 3.3 V transceiver, for example SN65HVD230 |
| GPIO5 | RXD | |
| 3V3 | VCC | 100 nF to GND at the pin |
| GND | GND | |

CANH and CANL go to the bus, with 120 ohm termination at both ends and the bus at 500 kbit/s. Change the pins in
`Esp32CanopenClientPins`.

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh canopen_client_basic_example Debug
./scripts/flash_app.sh flash_monitor canopen_client_basic_example Debug
```

`idf.py menuconfig` shows "CANopen client" under Component config: the SDO, guarding and drive timeouts, the safety
attempts and the two options that `canopen_client::KconfigDefaults()` returns.

## Expected output

```text
I (312) canopen_client_example: canopen_client driver 0.1.0
I (820) canopen_client_example: node 5 alive, 1 heartbeats
```

`node 5 silent` means no heartbeat arrived within three seconds: check the bit rate, the termination and that the node
produces a heartbeat (object 0x1017).

## Files

| File | What it is |
| --- | --- |
| [`main/canopen_client_basic_example.cpp`](main/canopen_client_basic_example.cpp) | The example app |
| [`main/esp32_canopen_client_port.hpp`](main/esp32_canopen_client_port.hpp) | The ESP-IDF port: TWAI frames, clock, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |

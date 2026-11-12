/**
 * @file esp32_canopen_client_port.hpp
 * @brief ESP-IDF port for the CANopen client driver: the TWAI controller for the frames, esp_timer
 * for the clock and ESP_LOG for the log hook.
 *
 * Wire the TWAI pins to a 3.3 V CAN transceiver (for example SN65HVD230) and the transceiver to the
 * bus with 120 ohm termination at both ends. Extended-identifier frames are dropped; the driver
 * uses 11-bit identifiers only. Not verified on hardware.
 */
#ifndef CANOPEN_CLIENT_ESP32_CANOPEN_CLIENT_PORT_HPP
#define CANOPEN_CLIENT_ESP32_CANOPEN_CLIENT_PORT_HPP

#include <cstdint>

#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "canopen_client.hpp"

/// Pins and speed of the ESP-IDF port (ESP32-C6 DevKitC-1, 500 kbit/s).
struct Esp32CanopenClientPins {
  int tx = 4; ///< TWAI TX, to the transceiver's TXD.
  int rx = 5; ///< TWAI RX, from the transceiver's RXD.
};

/// The CAN port on ESP-IDF's TWAI controller, with every optional hook.
class Esp32CanopenClientPort : public canopen_client::CanInterface<Esp32CanopenClientPort> {
public:
  explicit Esp32CanopenClientPort(const Esp32CanopenClientPins& pins = {}) noexcept : pins_(pins) {}

  /// Install and start the controller at 500 kbit/s, once.
  bool EnsureInitialized() noexcept {
    if (ready_) {
      return true;
    }
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(pins_.tx), static_cast<gpio_num_t>(pins_.rx), TWAI_MODE_NORMAL);
    general.rx_queue_len = 32;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
      return false;
    }
    if (twai_start() != ESP_OK) {
      static_cast<void>(twai_driver_uninstall());
      return false;
    }
    ready_ = true;
    return true;
  }

  /// Queue one frame; false when the queue is full or the controller is bus-off.
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    twai_message_t message{};
    message.identifier = frame.id;
    message.data_length_code = frame.dlc;
    message.rtr = frame.rtr ? 1U : 0U;
    for (uint8_t i = 0; (i < frame.dlc) && !frame.rtr; ++i) {
      message.data[i] = frame.data[i];
    }
    return twai_transmit(&message, 0) == ESP_OK;
  }

  /// Wait up to `timeout_us` (rounded up to a whole tick, never to zero) for one 11-bit frame.
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    const TickType_t ticks =
        (timeout_us == 0U)
            ? 0
            : static_cast<TickType_t>(
                  ((static_cast<uint64_t>(timeout_us) * configTICK_RATE_HZ) + 999'999U) /
                  1'000'000U);
    twai_message_t message{};
    const esp_err_t err = twai_receive(&message, ticks);
    if (err == ESP_ERR_TIMEOUT) {
      return canopen_client::RxStatus::Timeout;
    }
    if (err != ESP_OK) {
      return canopen_client::RxStatus::Error;
    }
    if ((message.extd != 0U) || (message.dlc_non_comp != 0U) || (message.data_length_code > 8U)) {
      return canopen_client::RxStatus::Timeout; // not a classic 11-bit frame: dropped
    }
    frame = canopen_client::CanFrame{};
    frame.id = static_cast<uint16_t>(message.identifier);
    frame.dlc = message.data_length_code;
    frame.rtr = message.rtr != 0U;
    for (uint8_t i = 0; (i < frame.dlc) && !frame.rtr; ++i) {
      frame.data[i] = message.data[i];
    }
    return canopen_client::RxStatus::Frame;
  }

  /// A free-running microsecond clock (wraps every 71 minutes, which the driver's subtractions
  /// allow).
  uint32_t NowUs() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() & 0xFFFF'FFFFLL);
  }

  /// Driver messages go to the ESP-IDF log.
  void Log(canopen_client::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level =
        (level == canopen_client::LogLevel::Error) ? ESP_LOG_ERROR : ESP_LOG_INFO;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  static constexpr const char* kTag = "canopen_client";
  Esp32CanopenClientPins pins_;
  bool ready_ = false;
};

#endif // CANOPEN_CLIENT_ESP32_CANOPEN_CLIENT_PORT_HPP

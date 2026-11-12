/**
 * @file canopen_client_basic_example.cpp
 * @brief Start the CANopen client from the menuconfig values, start node 5 and log its heartbeat
 * and emergency messages.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "canopen_client.hpp"
#include "canopen_client_kconfig.hpp"
#include "esp32_canopen_client_port.hpp"

namespace {
constexpr const char* kTag = "canopen_client_example";
constexpr uint8_t kNode = 5;
} // namespace

extern "C" void app_main(void) {
  static Esp32CanopenClientPort port;
  static canopen_client::Client<Esp32CanopenClientPort> client(
      port,
      canopen_client::KconfigDefaults()); // no I/O

  ESP_LOGI(kTag, "canopen_client driver %s", canopen_client::GetDriverVersion());
  if (const auto init = client.EnsureInitialized(); !init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", canopen_client::ToString(init.error));
    return;
  }
  if (const auto r = client.MonitorNode(kNode, 3'000'000); !r) {
    ESP_LOGE(kTag, "MonitorNode failed: %s", canopen_client::ToString(r.error));
    return;
  }
  if (const auto r = client.SendNmt(canopen_client::NmtCommand::Start, kNode); !r) {
    ESP_LOGE(kTag, "SendNmt failed: %s", canopen_client::ToString(r.error));
    return;
  }
  while (true) {
    const auto frame = client.Poll(500'000);
    if (!frame) {
      ESP_LOGW(kTag, "Poll failed: %s", canopen_client::ToString(frame.error));
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    canopen_client::EmcyEvent event{};
    while (client.PopEmcy(event)) {
      ESP_LOGW(kTag, "EMCY from node %u: code 0x%04X", static_cast<unsigned>(event.node),
               static_cast<unsigned>(event.code));
    }
    const auto status = client.GetNodeStatus(kNode);
    if (status && status.value.alive) {
      ESP_LOGI(kTag, "node %u alive, %lu heartbeats", static_cast<unsigned>(kNode),
               static_cast<unsigned long>(status.value.count));
    } else {
      ESP_LOGW(kTag, "node %u silent", static_cast<unsigned>(kNode));
    }
  }
}

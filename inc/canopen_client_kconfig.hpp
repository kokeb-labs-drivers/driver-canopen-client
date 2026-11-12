/**
 * @file canopen_client_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_CANOPEN_CLIENT_*` Kconfig
 * options.
 * @copyright Copyright (c) 2026 The driver-canopen-client authors. SPDX-License-Identifier: MIT
 *
 * Include it only where menuconfig should choose the defaults; without Kconfig `KconfigDefaults()`
 * equals `Config{}`. One configuration per program: every translation unit of one program must see
 * the same `CONFIG_` values, and no driver header includes this one (handbook KB-CPP20-018).
 */
#ifndef CANOPEN_CLIENT_KCONFIG_HPP
#define CANOPEN_CLIENT_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "canopen_client_types.hpp"

#ifndef CONFIG_CANOPEN_CLIENT_SDO_TIMEOUT_MS
/** SDO reply timeout when Kconfig does not set it: `Config{}.sdo_timeout_us` in milliseconds. */
#define CONFIG_CANOPEN_CLIENT_SDO_TIMEOUT_MS 200
#endif
#ifndef CONFIG_CANOPEN_CLIENT_GUARD_TIMEOUT_MS
/** Node-guarding reply timeout when Kconfig does not set it. */
#define CONFIG_CANOPEN_CLIENT_GUARD_TIMEOUT_MS 100
#endif
#ifndef CONFIG_CANOPEN_CLIENT_DRIVE_TIMEOUT_MS
/** Drive state change timeout when Kconfig does not set it. */
#define CONFIG_CANOPEN_CLIENT_DRIVE_TIMEOUT_MS 2000
#endif
#ifndef CONFIG_CANOPEN_CLIENT_DRIVE_POLL_INTERVAL_MS
/** Pause between statusword reads when Kconfig does not set it. */
#define CONFIG_CANOPEN_CLIENT_DRIVE_POLL_INTERVAL_MS 10
#endif
#ifndef CONFIG_CANOPEN_CLIENT_SAFETY_ATTEMPTS
/** Sends of a stop or disable command when Kconfig does not set it. */
#define CONFIG_CANOPEN_CLIENT_SAFETY_ATTEMPTS 3
#endif

namespace canopen_client {

/**
 * @brief The configuration menuconfig selected, on top of the defaults.
 *
 * A Kconfig `bool` is defined when it is `y` and absent when it is `n`, so the booleans follow
 * Kconfig only when this component's Kconfig is active (`CONFIG_CANOPEN_CLIENT` is defined);
 * otherwise they keep the `Config{}` defaults.
 *
 * @return `Config{}` with the timeouts and options from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
  c.sdo_timeout_us = static_cast<uint32_t>(CONFIG_CANOPEN_CLIENT_SDO_TIMEOUT_MS) * 1000U;
  c.guard_timeout_us = static_cast<uint32_t>(CONFIG_CANOPEN_CLIENT_GUARD_TIMEOUT_MS) * 1000U;
  c.drive_timeout_us = static_cast<uint32_t>(CONFIG_CANOPEN_CLIENT_DRIVE_TIMEOUT_MS) * 1000U;
  c.drive_poll_interval_us =
      static_cast<uint32_t>(CONFIG_CANOPEN_CLIENT_DRIVE_POLL_INTERVAL_MS) * 1000U;
  c.safety_attempts = static_cast<uint8_t>(CONFIG_CANOPEN_CLIENT_SAFETY_ATTEMPTS);
#ifdef CONFIG_CANOPEN_CLIENT
#ifdef CONFIG_CANOPEN_CLIENT_ABORT_ON_TIMEOUT
  c.abort_on_timeout = true;
#else
  c.abort_on_timeout = false;
#endif
#ifdef CONFIG_CANOPEN_CLIENT_STRICT_SDO_LENGTH
  c.strict_sdo_length = true;
#else
  c.strict_sdo_length = false;
#endif
#endif
  return c;
}

} // namespace canopen_client

#endif // CANOPEN_CLIENT_KCONFIG_HPP

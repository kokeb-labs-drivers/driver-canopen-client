/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): Config{} without Kconfig, the menuconfig values with it (KCONFIG_SET).
 */
#include "canopen_client_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr canopen_client::Config kCfg = canopen_client::KconfigDefaults();
  CHECK_EQ(kCfg.sdo_timeout_us, 500'000U);
  CHECK_EQ(kCfg.guard_timeout_us, 40'000U);
  CHECK_EQ(kCfg.drive_timeout_us, 3'000'000U);
  CHECK_EQ(kCfg.drive_poll_interval_us, 5'000U);
  CHECK_EQ(kCfg.safety_attempts, 5);
#ifdef KCONFIG_BOOLS_Y
  CHECK(kCfg.abort_on_timeout);
  CHECK(kCfg.strict_sdo_length);
#else
  CHECK(!kCfg.abort_on_timeout);
  CHECK(!kCfg.strict_sdo_length);
#endif
  CHECK(canopen_client::Validate(kCfg) == canopen_client::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr canopen_client::Config kCfg = canopen_client::KconfigDefaults();
  constexpr canopen_client::Config kDefault{};
  CHECK_EQ(kCfg.sdo_timeout_us, kDefault.sdo_timeout_us);
  CHECK_EQ(kCfg.guard_timeout_us, kDefault.guard_timeout_us);
  CHECK_EQ(kCfg.drive_timeout_us, kDefault.drive_timeout_us);
  CHECK_EQ(kCfg.drive_poll_interval_us, kDefault.drive_poll_interval_us);
  CHECK_EQ(kCfg.safety_attempts, kDefault.safety_attempts);
  CHECK_EQ(kCfg.abort_on_timeout, kDefault.abort_on_timeout);
  CHECK_EQ(kCfg.strict_sdo_length, kDefault.strict_sdo_length);
}
#endif

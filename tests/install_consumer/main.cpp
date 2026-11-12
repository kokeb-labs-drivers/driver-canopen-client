/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstdint>

#include "canopen_client.hpp"

namespace {

// A bus with nothing on it: frames go nowhere and no frame ever arrives. Enough to instantiate the
// whole driver template, including the receive path.
class EmptyPort : public canopen_client::CanInterface<EmptyPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Send(const canopen_client::CanFrame& frame) noexcept {
    (void)frame;
    return true;
  }
  canopen_client::RxStatus Receive(canopen_client::CanFrame& frame, uint32_t timeout_us) noexcept {
    (void)frame;
    now_us_ += timeout_us;
    return canopen_client::RxStatus::Timeout;
  }
  [[nodiscard]] uint32_t NowUs() const noexcept {
    return now_us_;
  }

private:
  uint32_t now_us_ = 0;
};

} // namespace

int main() {
  EmptyPort port;
  canopen_client::Client<EmptyPort> client(port);
  if (!client.EnsureInitialized().ok()) {
    return 1;
  }
  // Nobody answers an SDO request on an empty bus: the call must end with Timeout, not hang.
  const auto r = client.SdoReadU32(5, 0x1000, 0);
  return (r.error == canopen_client::Error::Timeout &&
          canopen_client::GetDriverVersion()[0] != '\0')
             ? 0
             : 1;
}

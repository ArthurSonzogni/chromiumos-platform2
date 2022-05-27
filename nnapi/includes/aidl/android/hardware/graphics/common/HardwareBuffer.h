// This code is a basic stub to represent the HardwareBuffer
// type that is referenced in the generated AIDL code.
//
// It is unused within the runtime, since we don't support
// hardware buffers, however it is required for the system
// to compile unmodified.

#pragma once

#include <android/binder_interface_utils.h>
#include <android/binder_parcelable_utils.h>
#include <android/binder_to_string.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#ifdef BINDER_STABILITY_SUPPORT
#include <android/binder_stability.h>
#endif  // BINDER_STABILITY_SUPPORT

namespace aidl {
namespace android {
namespace hardware {
namespace graphics {
namespace common {
class HardwareBuffer {
 public:
  static const char* descriptor;
  int placeholder;

  binder_status_t readFromParcel(const AParcel*) { return 0; }
  binder_status_t writeToParcel(AParcel*) const { return 0; }

  inline bool operator!=(const HardwareBuffer& rhs) const {
    return placeholder != rhs.placeholder;
  }
  inline bool operator<(const HardwareBuffer& rhs) const {
    return placeholder < rhs.placeholder;
  }
  inline bool operator<=(const HardwareBuffer& rhs) const {
    return placeholder <= rhs.placeholder;
  }
  inline bool operator==(const HardwareBuffer& rhs) const {
    return placeholder == rhs.placeholder;
  }
  inline bool operator>(const HardwareBuffer& rhs) const {
    return placeholder > rhs.placeholder;
  }
  inline bool operator>=(const HardwareBuffer& rhs) const {
    return placeholder >= rhs.placeholder;
  }
};
}  // namespace common
}  // namespace graphics
}  // namespace hardware
}  // namespace android
}  // namespace aidl
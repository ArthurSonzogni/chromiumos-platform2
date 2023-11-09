// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCROSSYSTEM_CROSSYSTEM_H_
#define LIBCROSSYSTEM_CROSSYSTEM_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <brillo/brillo_export.h>

namespace crossystem {

class CrossystemVbootInterface;

// C++ class to access crossystem system properties.
class BRILLO_EXPORT Crossystem {
 public:
  // Default implementation uses the real crossystem (CrossystemImpl).
  Crossystem();

  // Can be used to instantiate a fake implementation for testing by passing
  // CrossystemFake.
  explicit Crossystem(std::unique_ptr<CrossystemVbootInterface> impl);

  ~Crossystem();

  std::optional<int> VbGetSystemPropertyInt(const std::string& name) const;

  bool VbSetSystemPropertyInt(const std::string& name, int value);

  std::optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const;

  bool VbSetSystemPropertyString(const std::string& name,
                                 const std::string& value);

  /// Reads a system property boolean.
  ///
  /// @param name The name of the target system property.
  /// @return The property value, or |base::nullopt| if error.
  std::optional<bool> GetSystemPropertyBool(const std::string& name) const;

  /// Sets a system property boolean.
  ///
  /// @param name The name of the target system property.
  /// @param value The boolean value to set.
  /// @return |true| if it succeeds; |false| if it fails.
  bool SetSystemPropertyBool(const std::string& name, bool value);

  /// Get hardware write protect status.
  ///
  /// @note Crashes if the underlying status is not set or set to an invalid
  /// value.
  ///
  /// @return true if hardware write protect is enabled; false otherwise.
  bool HardwareWriteProtectIsEnabled() const;

  /// Get hardware ID.
  ///
  /// @return hardware ID string
  std::string GetHardwareID() const;

  /// Check if system is configured to only boot from a signed kernel.
  ///
  /// @note Crashes if the underlying status is not set or set to an invalid
  /// value.
  ///
  /// @return true if only signed kernels will boot; false otherwise.
  bool OnlyBootSignedKernel() const;

  /// Get the priority slot of MiniOS.
  ///
  /// @return slot of the priority minios if present, nullopt otherwise.
  std::optional<std::string> GetMiniOsPriority() const;

  // Use the helper methods (e.g., HardwareProtectIsEnabled()) rather than
  // using these constants directly.
  BRILLO_PRIVATE static constexpr char kHardwareWriteProtect[] = "wpsw_cur";
  BRILLO_PRIVATE static constexpr char kHardwareId[] = "hwid";
  BRILLO_PRIVATE static constexpr char kDevBootSignedOnly[] =
      "dev_boot_signed_only";

  // Name of property containing the position of the Developer Switch when the
  // device booted.
  BRILLO_PRIVATE static constexpr char kDevSwitchBoot[] = "devsw_boot";

  // Name of property indicating if we booted in developer mode.
  BRILLO_PRIVATE static constexpr char kCrosDebug[] = "cros_debug";

  // Name of property containing the active main firmware.
  BRILLO_PRIVATE static constexpr char kMainFirmwareActive[] = "mainfw_act";

  // Name of property containing the main firmware type.
  BRILLO_PRIVATE static constexpr char kMainFirmwareType[] = "mainfw_type";
  BRILLO_PRIVATE static constexpr char kMainfwTypeNonchrome[] = "nonchrome";

  // Name of property that signals a request to clear TPM owner on next reboot.
  BRILLO_PRIVATE static constexpr char kClearTpmOwnerRequest[] =
      "clear_tpm_owner_request";

  // Name of property that indicates if the current build is a debug build.
  BRILLO_PRIVATE static constexpr char kDebugBuild[] = "debug_build";

  // Name of property that contains the recovery reason.
  BRILLO_PRIVATE static constexpr char kRecoveryReason[] = "recovery_reason";

  // Name of property that indicates if nvram has been cleared.
  BRILLO_PRIVATE static constexpr char kNvramCleared[] = "nvram_cleared";

  // Name of property that indicates if dev mode is blocked.
  BRILLO_PRIVATE static constexpr char kBlockDevmode[] = "block_devmode";

  // Name of property contianing the enrollment status.
  BRILLO_PRIVATE static constexpr char kCheckEnrollment[] = "check_enrollment";

  // Name of property that indicates minios priority slot.
  BRILLO_PRIVATE static constexpr char kMiniosPriorityProperty[] =
      "minios_priority";

 private:
  std::unique_ptr<CrossystemVbootInterface> impl_;
};

}  // namespace crossystem

#endif  // LIBCROSSYSTEM_CROSSYSTEM_H_

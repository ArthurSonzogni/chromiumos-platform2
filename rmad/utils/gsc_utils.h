// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GSC_UTILS_H_
#define RMAD_UTILS_GSC_UTILS_H_

#include <optional>
#include <string>

namespace rmad {

enum class SpiAddressingMode {
  kUnknown = 0,
  k3Byte = 1,
  k4Byte = 2,
  kNotProvisioned = 3,
};

struct FactoryConfig {
  bool is_chassis_branded;
  int hw_compliance_version;
};

class GscUtils {
 public:
  GscUtils() = default;
  virtual ~GscUtils() = default;

  // Get the RSU challenge code. Return true if successfully get the challenge
  // code, false if failed to get the challenge code.
  virtual bool GetRsuChallengeCode(std::string* challenge_code) const = 0;

  // Use the unlock code to perform RSU. Return true if the unlock code is
  // correct, false if the unlock code is rejected.
  virtual bool PerformRsu(const std::string& unlock_code) const = 0;

  // Enable factory mode. Return true if the command succeeds, or factory mode
  // is already enabled, false if fails to enable factory mode (e.g. blocked by
  // FWMP).
  virtual bool EnableFactoryMode() const = 0;

  // Disable factory mode. Return true if the command succeeds, false if fails
  // to disable factory mode.
  virtual bool DisableFactoryMode() const = 0;

  // Check if GSC factory mode is enabled.
  virtual bool IsFactoryModeEnabled() const = 0;

  // Check if GSC initial factory mode is enabled.
  virtual bool IsInitialFactoryModeEnabled() const = 0;

  // Get GSC board ID type.
  virtual std::optional<std::string> GetBoardIdType() const = 0;

  // Get GSC board ID flags.
  virtual std::optional<std::string> GetBoardIdFlags() const = 0;

  // Set GSC board ID.
  virtual bool SetBoardId(bool is_custom_label) const = 0;

  // Do a GSC reboot.
  virtual bool Reboot() const = 0;

  // Get factory config.
  virtual std::optional<FactoryConfig> GetFactoryConfig() const = 0;

  // Set factory config.
  virtual bool SetFactoryConfig(bool is_chassis_branded,
                                int hw_compliance_version) const = 0;

  // Get CHASSIS_OPEN status.
  virtual std::optional<bool> GetChassisOpenStatus() = 0;

  // Get addressing mode of Ti50 device.
  virtual SpiAddressingMode GetAddressingMode() = 0;

  // Set addressing mode for ap ro verification on Ti50.
  virtual bool SetAddressingMode(SpiAddressingMode mode) = 0;

  // Decide the addressing mode by the given flash size.
  virtual SpiAddressingMode GetAddressingModeByFlashSize(
      uint64_t flash_size) = 0;

  // Set wpsr for ap ro verification on Ti50.
  virtual bool SetWpsr(std::string_view wpsr) = 0;

  // Check if wpsr is already provisioned.
  virtual std::optional<bool> IsApWpsrProvisioned() = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GSC_UTILS_H_

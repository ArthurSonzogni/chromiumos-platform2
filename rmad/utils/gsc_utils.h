// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_GSC_UTILS_H_
#define RMAD_UTILS_GSC_UTILS_H_

#include <string>

namespace rmad {

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
  virtual bool GetBoardIdType(std::string* board_id_type) const = 0;

  // Get GSC board ID flags.
  virtual bool GetBoardIdFlags(std::string* board_id_flags) const = 0;

  // Set GSC board ID.
  virtual bool SetBoardId(bool is_custom_label) const = 0;

  // Do a GSC reboot.
  virtual bool Reboot() const = 0;

  // Get factory config.
  virtual bool GetFactoryConfig(bool* is_chassis_branded,
                                int* hw_compliance_version) const = 0;

  // Set factory config.
  virtual bool SetFactoryConfig(bool is_chassis_branded,
                                int hw_compliance_version) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_GSC_UTILS_H_

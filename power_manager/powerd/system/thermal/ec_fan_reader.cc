// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/thermal/ec_fan_reader.h"

#include <fcntl.h>

#include <memory>

#include <base/files/scoped_file.h>
#include <chromeos/ec/ec_commands.h>
#include <libec/pwm/pwm_get_fan_target_rpm_command.h>

namespace power_manager::system {

void EcFanReader::Init(const base::FilePath& cros_ec_path,
                       ec::EcCommandFactoryInterface* ec_command_factory) {
  cros_ec_path_ = cros_ec_path;
  ec_command_factory_ = ec_command_factory;
}

std::optional<uint16_t> EcFanReader::GetCurrentHighestFanSpeed() {
  base::ScopedFD ec_fd =
      base::ScopedFD(open(cros_ec_path_.value().c_str(), O_RDONLY));

  if (!ec_fd.is_valid()) {
    // This is expected on systems without the CrOS EC.
    LOG(INFO) << "Failed to open " << cros_ec_path_;
    return std::nullopt;
  }

  std::unique_ptr<ec::GetFeaturesCommand> get_features =
      ec_command_factory_->GetFeaturesCommand();

  // check if fan feature is supported
  if (!get_features || !get_features->Run(ec_fd.get())) {
    LOG(ERROR) << "Failed to run ec::GetFeaturesCommand";
    return std::nullopt;
  }

  if (!get_features->IsFeatureSupported(EC_FEATURE_PWM_FAN)) {
    return std::nullopt;
  }

  uint16_t highest_fan_rpm = 0;
  for (uint8_t fan_idx = 0; fan_idx < EC_FAN_SPEED_ENTRIES; ++fan_idx) {
    std::unique_ptr<ec::PwmGetFanTargetRpmCommand> get_fan_rpm =
        ec_command_factory_->PwmGetFanTargetRpmCommand(fan_idx);
    if (!get_fan_rpm || !get_fan_rpm->Run(ec_fd.get()) ||
        !get_fan_rpm->Rpm().has_value()) {
      LOG(ERROR) << "Failed to read fan speed for fan idx: "
                 << static_cast<int>(fan_idx) << " from EC";
      return std::nullopt;
    }

    // If the fan speed is not present, that means that the ec fan slot and
    // the ones after it are not initialized and we may safely ignore them
    if (get_fan_rpm->Rpm().value() == EC_FAN_SPEED_NOT_PRESENT) {
      break;
    }

    if (get_fan_rpm->Rpm().value() > highest_fan_rpm) {
      highest_fan_rpm = get_fan_rpm->Rpm().value();
    }
  }

  return highest_fan_rpm;
}

}  // namespace power_manager::system

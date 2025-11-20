// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/thermal/ec_fan_reader_stub.h"

#include <base/check.h>

namespace power_manager::system {

void EcFanReaderStub::Init(const base::FilePath& cros_ec_path,
                           ec::EcCommandFactoryInterface* ec_command_factory) {
  cros_ec_path_ = cros_ec_path;
  ec_command_factory_ = ec_command_factory;
}

std::optional<uint16_t> EcFanReaderStub::GetCurrentHighestFanSpeed() {
  return current_highest_fan_speed_;
}

void EcFanReaderStub::SetCurrentHighestFanSpeed(std::optional<uint16_t> val) {
  current_highest_fan_speed_ = val;
}

}  // namespace power_manager::system

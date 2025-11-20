// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_THERMAL_EC_FAN_READER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_THERMAL_EC_FAN_READER_STUB_H_

#include "power_manager/powerd/system/thermal/ec_fan_reader.h"

namespace power_manager::system {

class EcFanReaderStub : public EcFanReaderInterface {
 public:
  EcFanReaderStub() = default;
  EcFanReaderStub(const EcFanReaderStub&) = delete;
  EcFanReaderStub& operator=(const EcFanReaderStub&) = delete;

  ~EcFanReaderStub() override = default;

  void Init(const base::FilePath& cros_ec_path,
            ec::EcCommandFactoryInterface* ec_command_factory) override;
  std::optional<uint16_t> GetCurrentHighestFanSpeed() override;

  void SetCurrentHighestFanSpeed(std::optional<uint16_t> value);

 private:
  base::FilePath cros_ec_path_;
  ec::EcCommandFactoryInterface* ec_command_factory_ = nullptr;  // non-owned
  std::optional<uint16_t> current_highest_fan_speed_ = std::nullopt;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_THERMAL_EC_FAN_READER_STUB_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_FILE_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_FILE_H_

#include <memory>
#include <vector>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/ambient_light_sensor.h"
#include "power_manager/powerd/system/ambient_light_sensor_delegate_file.h"
#include "power_manager/powerd/system/ambient_light_sensor_manager_interface.h"

namespace power_manager {

class PrefsInterface;

namespace system {

class AmbientLightSensorManagerFile
    : public AmbientLightSensorManagerInterface {
 public:
  explicit AmbientLightSensorManagerFile(PrefsInterface* prefs);
  AmbientLightSensorManagerFile(const AmbientLightSensorManagerFile&) = delete;
  AmbientLightSensorManagerFile& operator=(
      const AmbientLightSensorManagerFile&) = delete;

  ~AmbientLightSensorManagerFile() override;

  void set_device_list_path_for_testing(const base::FilePath& path);
  void set_poll_interval_ms_for_testing(int interval_ms);

  void Run(bool read_immediately);

  bool HasColorSensor() override;

  // AmbientLightSensorManagerInterface overrides:
  AmbientLightSensorInterface* GetSensorForInternalBacklight() override;
  AmbientLightSensorInterface* GetSensorForKeyboardBacklight() override;

 private:
  std::unique_ptr<AmbientLightSensor> CreateSensor(SensorLocation location,
                                                   bool allow_ambient_eq);

  PrefsInterface* prefs_ = nullptr;  // non-owned

  std::vector<std::unique_ptr<system::AmbientLightSensor>> sensors_;
  // Weak pointers into the relevant entries of |sensors_|.
  system::AmbientLightSensor* lid_sensor_ = nullptr;
  system::AmbientLightSensor* base_sensor_ = nullptr;

  std::vector<AmbientLightSensorDelegateFile*> als_list_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_FILE_H_

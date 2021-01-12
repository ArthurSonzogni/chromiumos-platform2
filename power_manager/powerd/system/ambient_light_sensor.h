// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_

#include <memory>

#include <base/observer_list.h>
#include <base/optional.h>

#include "power_manager/powerd/system/ambient_light_sensor_delegate.h"
#include "power_manager/powerd/system/ambient_light_sensor_interface.h"

namespace power_manager {
namespace system {

class AmbientLightSensor : public AmbientLightSensorInterface {
 public:
  AmbientLightSensor() = default;
  AmbientLightSensor(const AmbientLightSensor&) = delete;
  AmbientLightSensor& operator=(const AmbientLightSensor&) = delete;
  ~AmbientLightSensor() override = default;

  void SetDelegate(std::unique_ptr<AmbientLightSensorDelegate> delegate);

  // AmbientLightSensorInterface implementation:
  void AddObserver(AmbientLightObserver* observer) override;
  void RemoveObserver(AmbientLightObserver* observer) override;
  bool IsColorSensor() const override;
  int GetAmbientLightLux() override;
  int GetColorTemperature() override;
  base::FilePath GetIlluminancePath() const override;

 private:
  void SetLuxAndColorTemperature(base::Optional<int> lux,
                                 base::Optional<int> color_temperature);

  // List of backlight controllers that are currently interested in updates from
  // this sensor.
  base::ObserverList<AmbientLightObserver> observers_;

  // Lux value read by the class. If this read did not succeed or no read has
  // occurred yet this variable is set to -1.
  int lux_value_ = -1;

  // Color temperature read by the class. If this read did not succeed or no
  // read has occurred yet this variable is set to -1.
  int color_temperature_ = -1;

  std::unique_ptr<AmbientLightSensorDelegate> delegate_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_

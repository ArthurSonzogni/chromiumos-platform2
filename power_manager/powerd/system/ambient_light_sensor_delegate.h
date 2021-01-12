// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_DELEGATE_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_DELEGATE_H_

#include <map>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/optional.h>

namespace power_manager {
namespace system {

enum class ChannelType {
  X,
  Y,
  Z,
};

const struct ColorChannelInfo {
  ChannelType type;
  const char* rgb_name;
  const char* xyz_name;
  bool is_lux_channel;
} kColorChannelConfig[] = {
    {ChannelType::X, "red", "x", false},
    {ChannelType::Y, "green", "y", true},
    {ChannelType::Z, "blue", "z", false},
};

enum class SensorLocation {
  UNKNOWN,
  BASE,
  LID,
};

class AmbientLightSensorDelegate {
 public:
  // |readings[ChannelType::X]|: red color reading value.
  // |readings[ChannelType::Y]|: green color reading value.
  // |readings[ChannelType::Z]|: blue color reading value.
  // Returns base::nullopt if the color temperature is unavailable.
  static base::Optional<int> CalculateColorTemperature(
      const std::map<ChannelType, int>& readings);

  AmbientLightSensorDelegate() {}
  AmbientLightSensorDelegate(const AmbientLightSensorDelegate&) = delete;
  AmbientLightSensorDelegate& operator=(const AmbientLightSensorDelegate&) =
      delete;
  virtual ~AmbientLightSensorDelegate() {}

  virtual bool IsColorSensor() const = 0;
  virtual base::FilePath GetIlluminancePath() const = 0;

  void SetLuxCallback(
      base::RepeatingCallback<void(base::Optional<int>, base::Optional<int>)>
          set_lux_callback);

 protected:
  base::RepeatingCallback<void(base::Optional<int>, base::Optional<int>)>
      set_lux_callback_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_DELEGATE_H_

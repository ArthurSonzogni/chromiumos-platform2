// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_delegate.h"

#include <utility>

#include <base/logging.h>

namespace power_manager {
namespace system {

// static
base::Optional<int> AmbientLightSensorDelegate::CalculateColorTemperature(
    const std::map<ChannelType, int>& readings) {
  const auto it_x = readings.find(ChannelType::X),
             it_y = readings.find(ChannelType::Y),
             it_z = readings.find(ChannelType::Z);
  if (it_x == readings.end() || it_y == readings.end() ||
      it_z == readings.end()) {
    return base::nullopt;
  }

  double scale_factor = it_x->second + it_y->second + it_z->second;
  if (scale_factor <= 0.0)
    return base::nullopt;

  double scaled_x = it_x->second / scale_factor;
  double scaled_y = it_y->second / scale_factor;
  // Avoid weird behavior around the function's pole.
  if (scaled_y < 0.186)
    return base::nullopt;

  double n = (scaled_x - 0.3320) / (0.1858 - scaled_y);

  int color_temperature =
      static_cast<int>(449 * n * n * n + 3525 * n * n + 6823.3 * n + 5520.33);
  VLOG(1) << "Color temperature: " << color_temperature;

  return color_temperature;
}

void AmbientLightSensorDelegate::SetLuxCallback(
    base::RepeatingCallback<void(base::Optional<int>, base::Optional<int>)>
        set_lux_callback) {
  set_lux_callback_ = std::move(set_lux_callback);
}

}  // namespace system
}  // namespace power_manager

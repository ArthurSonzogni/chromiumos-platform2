// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/macros.h>

#include "mems_setup/sensor_kind.h"

namespace mems_setup {

namespace {
constexpr char kAccelName[] = "accel";
constexpr char kGyroName[] = "anglvel";
constexpr char kLightName[] = "illuminance";
constexpr char kSyncName[] = "count";
constexpr char kMagnName[] = "magn";
constexpr char kLidAngleName[] = "angl";
constexpr char kBaroName[] = "baro";
constexpr char kOthersName[] = "";

constexpr char kAccelDeviceName[] = "cros-ec-accel";
constexpr char kGyroDeviceName[] = "cros-ec-gyro";
constexpr char kLightDeviceName[] = "cros-ec-light";
constexpr char kAlsDeviceName[] = "acpi-als";
constexpr char kSyncDeviceName[] = "cros-ec-sync";
constexpr char kMagnDeviceName[] = "cros-ec-mag";
constexpr char kLidAngleDeviceName[] = "cros-ec-lid-angle";
constexpr char kBaroDeviceName[] = "cros-ec-baro";
}  // namespace

std::string SensorKindToString(SensorKind kind) {
  switch (kind) {
    case SensorKind::ACCELEROMETER:
      return kAccelName;
    case SensorKind::GYROSCOPE:
      return kGyroName;
    case SensorKind::LIGHT:
      return kLightName;
    case SensorKind::SYNC:
      return kSyncName;
    case SensorKind::MAGNETOMETER:
      return kMagnName;
    case SensorKind::LID_ANGLE:
      return kLidAngleName;
    case SensorKind::BAROMETER:
      return kBaroName;
    case SensorKind::OTHERS:  // Shouldn't be used
      return kOthersName;
  }

  NOTREACHED();
}

SensorKind SensorKindFromString(const std::string& name) {
  if (name == kAccelDeviceName)
    return SensorKind::ACCELEROMETER;
  if (name == kGyroDeviceName)
    return SensorKind::GYROSCOPE;
  if (name == kLightDeviceName || name == kAlsDeviceName)
    return SensorKind::LIGHT;
  if (name == kSyncDeviceName)
    return SensorKind::SYNC;
  if (name == kMagnDeviceName)
    return SensorKind::MAGNETOMETER;
  if (name == kLidAngleDeviceName)
    return SensorKind::LID_ANGLE;
  if (name == kBaroDeviceName)
    return SensorKind::BAROMETER;

  return SensorKind::OTHERS;
}

}  // namespace mems_setup

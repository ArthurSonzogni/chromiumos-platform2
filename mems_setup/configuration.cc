// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mems_setup/configuration.h"

#include <initializer_list>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include <libmems/iio_channel.h>
#include <libmems/iio_context.h>
#include <libmems/iio_device.h>
#include "mems_setup/sensor_location.h"

namespace mems_setup {

namespace {

struct ImuVpdCalibrationEntry {
  std::string name;
  std::string calib;
  base::Optional<int> value;
  bool missing_is_error;
};

struct LightVpdCalibrationEntry {
  std::string vpd_name;
  std::string iio_name;
};

constexpr char kCalibrationBias[] = "bias";
constexpr char kCalibrationScale[] = "scale";

constexpr int kGyroMaxVpdCalibration = 16384;  // 16dps
constexpr int kAccelMaxVpdCalibration = 256;   // .250g
constexpr int kAccelSysfsTriggerId = 0;

constexpr std::initializer_list<const char*> kAccelAxes = {
    "x",
    "y",
    "z",
};
}  // namespace

Configuration::Configuration(libmems::IioDevice* sensor,
                             SensorKind kind,
                             Delegate* del)
    : delegate_(del), kind_(kind), sensor_(sensor) {}

bool Configuration::Configure() {
  switch (kind_) {
    case SensorKind::ACCELEROMETER:
      return ConfigAccelerometer();
    case SensorKind::GYROSCOPE:
      return ConfigGyro();
    case SensorKind::LIGHT:
      return ConfigIlluminance();
    default:
      LOG(ERROR) << SensorKindToString(kind_) << " unimplemented";
      return false;
  }
}

bool Configuration::CopyLightCalibrationFromVpd() {
  std::vector<LightVpdCalibrationEntry> calib_attributes = {
      {"als_cal_intercept", "in_illuminance_calibbias"},
      {"als_cal_slope", "in_illuminance_calibscale"},
  };

  for (auto& calib_attribute : calib_attributes) {
    auto attrib_value = delegate_->ReadVpdValue(calib_attribute.vpd_name);
    if (!attrib_value.has_value()) {
      LOG(ERROR) << "VPD missing calibration value "
                 << calib_attribute.vpd_name;
      continue;
    }

    double value;
    if (!base::StringToDouble(attrib_value.value(), &value)) {
      LOG(ERROR) << "VPD calibration value " << calib_attribute.vpd_name
                 << " has invalid value " << attrib_value.value();
      continue;
    }
    if (!sensor_->WriteDoubleAttribute(calib_attribute.iio_name, value))
      LOG(ERROR) << "failed to set calibration value "
                 << calib_attribute.iio_name;
  }

  return true;
}

bool Configuration::CopyImuCalibationFromVpd(int max_value) {
  if (sensor_->IsSingleSensor()) {
    auto location = sensor_->ReadStringAttribute("location");
    if (!location || location->empty()) {
      LOG(ERROR) << "cannot read a valid sensor location";
      return false;
    }
    return CopyImuCalibationFromVpd(max_value, location->c_str());
  } else {
    bool base_config = CopyImuCalibationFromVpd(max_value, kBaseSensorLocation);
    bool lid_config = CopyImuCalibationFromVpd(max_value, kLidSensorLocation);
    return base_config && lid_config;
  }
}

bool Configuration::CopyImuCalibationFromVpd(int max_value,
                                             const std::string& location) {
  const bool is_single_sensor = sensor_->IsSingleSensor();
  std::string kind = SensorKindToString(kind_);

  std::vector<ImuVpdCalibrationEntry> calib_attributes = {
      {"x", kCalibrationBias, base::nullopt, true},
      {"y", kCalibrationBias, base::nullopt, true},
      {"z", kCalibrationBias, base::nullopt, true},

      {"x", kCalibrationScale, base::nullopt, false},
      {"y", kCalibrationScale, base::nullopt, false},
      {"z", kCalibrationScale, base::nullopt, false},
  };

  for (auto& calib_attribute : calib_attributes) {
    auto attrib_name = base::StringPrintf(
        "in_%s_%s_%s_calib%s", kind.c_str(), calib_attribute.name.c_str(),
        location.c_str(), calib_attribute.calib.c_str());
    auto attrib_value = delegate_->ReadVpdValue(attrib_name.c_str());
    if (!attrib_value.has_value()) {
      if (calib_attribute.missing_is_error)
        LOG(ERROR) << "VPD missing calibration value " << attrib_name;
      continue;
    }

    int value;
    if (!base::StringToInt(attrib_value.value(), &value)) {
      LOG(ERROR) << "VPD calibration value " << attrib_name
                 << " has invalid value " << attrib_value.value();
      continue;
    }
    if (abs(value) > max_value) {
      LOG(ERROR) << "VPD calibration value " << attrib_name
                 << " has out-of-range value " << attrib_value.value();
      continue;
    } else {
      calib_attribute.value = value;
    }
  }

  for (const auto& calib_attribute : calib_attributes) {
    if (!calib_attribute.value)
      continue;
    auto attrib_name = base::StringPrintf("in_%s_%s", kind.c_str(),
                                          calib_attribute.name.c_str());

    if (!is_single_sensor)
      attrib_name =
          base::StringPrintf("%s_%s", attrib_name.c_str(), location.c_str());
    attrib_name = base::StringPrintf("%s_calib%s", attrib_name.c_str(),
                                     calib_attribute.calib.c_str());

    if (!sensor_->WriteNumberAttribute(attrib_name, *calib_attribute.value))
      LOG(ERROR) << "failed to set calibration value " << attrib_name;
  }

  LOG(INFO) << "VPD calibration complete";
  return true;
}

bool Configuration::AddSysfsTrigger(int trigger_id) {
  auto iio_trig = sensor_->GetContext()->GetDevice("iio_sysfs_trigger");
  if (iio_trig == nullptr) {
    LOG(ERROR) << "cannot find iio_trig_sysfs kernel module";
    return false;
  }

  // There is a potential cross-process race here, where multiple instances
  // of this tool may be trying to access the trigger at once. To solve this,
  // first see if the trigger is already there. If not, try to create it, and
  // then try to access it again. Only if the latter access fails then
  // error out.
  std::string trigger_name = base::StringPrintf("trigger%d", trigger_id);
  auto trigger = sensor_->GetContext()->GetDevice(trigger_name);
  if (trigger == nullptr) {
    LOG(INFO) << trigger_name << " not found; adding";
    if (!iio_trig->WriteNumberAttribute("add_trigger", trigger_id)) {
      LOG(WARNING) << "cannot instantiate trigger " << trigger_name;
    }

    sensor_->GetContext()->Reload();
    trigger = sensor_->GetContext()->GetDevice(trigger_name);
    if (trigger == nullptr) {
      LOG(ERROR) << "cannot find trigger device " << trigger_name;
      return false;
    }
  }

  if (!sensor_->SetTrigger(trigger)) {
    LOG(ERROR) << "cannot set sensor's trigger to " << trigger_name;
    return false;
  }

  base::FilePath trigger_now = trigger->GetPath().Append("trigger_now");

  base::Optional<gid_t> chronos_gid = delegate_->FindGroupId("chronos");
  if (!chronos_gid) {
    LOG(ERROR) << "chronos group not found";
    return false;
  }

  if (!delegate_->SetOwnership(trigger_now, -1, chronos_gid.value())) {
    LOG(ERROR) << "cannot configure ownership on the trigger";
    return false;
  }

  int permission = delegate_->GetPermissions(trigger_now);
  permission |= base::FILE_PERMISSION_WRITE_BY_GROUP;
  if (!delegate_->SetPermissions(trigger_now, permission)) {
    LOG(ERROR) << "cannot configure permissions on the trigger";
    return false;
  }

  LOG(INFO) << "sysfs trigger setup complete";
  return true;
}

bool Configuration::EnableAccelScanElements() {
  auto timestamp = sensor_->GetChannel("timestamp");
  if (!timestamp) {
    LOG(ERROR) << "cannot find timestamp channel";
    return false;
  }
  if (!timestamp->SetEnabledAndCheck(false)) {
    LOG(ERROR) << "failed to disable timestamp channel";
    return false;
  }

  std::vector<std::string> channels_to_enable;

  if (sensor_->IsSingleSensor()) {
    for (const auto& axis : kAccelAxes) {
      channels_to_enable.push_back(base::StringPrintf("accel_%s", axis));
    }
  } else {
    for (const auto& axis : kAccelAxes) {
      channels_to_enable.push_back(
          base::StringPrintf("accel_%s_%s", axis, kBaseSensorLocation));
      channels_to_enable.push_back(
          base::StringPrintf("accel_%s_%s", axis, kLidSensorLocation));
    }
  }

  for (const auto& chan_name : channels_to_enable) {
    auto channel = sensor_->GetChannel(chan_name);
    if (!channel) {
      LOG(ERROR) << "cannot find channel " << chan_name;
      return false;
    }
    if (!channel->SetEnabledAndCheck(true)) {
      LOG(ERROR) << "failed to enable channel " << chan_name;
      return false;
    }
  }

  sensor_->EnableBuffer(1);
  if (!sensor_->IsBufferEnabled()) {
    LOG(ERROR) << "failed to enable buffer";
    return false;
  }

  LOG(INFO) << "buffer enabled";
  return true;
}

bool Configuration::EnableKeyboardAngle() {
  base::FilePath kb_wake_angle;
  if (sensor_->IsSingleSensor()) {
    kb_wake_angle = base::FilePath("/sys/class/chromeos/cros_ec/kb_wake_angle");
  } else {
    kb_wake_angle = sensor_->GetPath().Append("in_angl_offset");
  }

  if (!delegate_->Exists(kb_wake_angle)) {
    LOG(INFO) << kb_wake_angle.value()
              << " not found; will not enable EC wake angle";
    return true;
  }

  base::Optional<gid_t> power_gid = delegate_->FindGroupId("power");
  if (!power_gid) {
    LOG(ERROR) << "cannot configure ownership on the wake angle file";
    return false;
  }

  delegate_->SetOwnership(kb_wake_angle, -1, power_gid.value());
  int permission = delegate_->GetPermissions(kb_wake_angle);
  permission |= base::FILE_PERMISSION_WRITE_BY_GROUP;
  delegate_->SetPermissions(kb_wake_angle, permission);

  LOG(INFO) << "keyboard angle enabled";
  return true;
}

bool Configuration::ConfigGyro() {
  if (!CopyImuCalibationFromVpd(kGyroMaxVpdCalibration))
    return false;

  LOG(INFO) << "gyroscope configuration complete";
  return true;
}

bool Configuration::ConfigAccelerometer() {
  if (!CopyImuCalibationFromVpd(kAccelMaxVpdCalibration))
    return false;

  if (!AddSysfsTrigger(kAccelSysfsTriggerId))
    return false;

  if (!EnableAccelScanElements())
    return false;

  if (!EnableKeyboardAngle())
    return false;

  LOG(INFO) << "accelerometer configuration complete";
  return true;
}

bool Configuration::ConfigIlluminance() {
  if (!CopyLightCalibrationFromVpd())
    return false;

  LOG(INFO) << "light configuration complete";
  return true;
}

}  // namespace mems_setup

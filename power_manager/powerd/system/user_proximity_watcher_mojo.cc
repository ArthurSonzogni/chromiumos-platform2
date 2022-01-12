// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/user_proximity_watcher_mojo.h"

#include <fcntl.h>
#include <linux/iio/events.h>
#include <linux/iio/types.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include <cros_config/cros_config.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/user_proximity_observer.h"

namespace power_manager {
namespace system {

UserProximityWatcherMojo::UserProximityWatcherMojo(
    PrefsInterface* prefs,
    std::unique_ptr<brillo::CrosConfigInterface> config,
    std::unique_ptr<libsar::SarConfigReader::Delegate> delegate,
    TabletMode tablet_mode,
    SensorServiceHandler* sensor_service_handler)
    : SensorServiceHandlerObserver(sensor_service_handler),
      config_(std::move(config)),
      delegate_(std::move(delegate)),
      tablet_mode_(tablet_mode) {
  prefs->GetBool(kSetCellularTransmitPowerForProximityPref,
                 &use_proximity_for_cellular_);
  prefs->GetBool(kSetWifiTransmitPowerForProximityPref,
                 &use_proximity_for_wifi_);

  prefs->GetBool(kSetCellularTransmitPowerForActivityProximityPref,
                 &use_activity_proximity_for_cellular_);
  prefs->GetBool(kSetWifiTransmitPowerForActivityProximityPref,
                 &use_activity_proximity_for_wifi_);
}

UserProximityWatcherMojo::~UserProximityWatcherMojo() = default;

void UserProximityWatcherMojo::HandleTabletModeChange(TabletMode mode) {
  if (tablet_mode_ == mode)
    return;

  tablet_mode_ = mode;
  // TODO(b/215726246): Check if we should compensate sensors.
}

void UserProximityWatcherMojo::OnNewDeviceAdded(
    int32_t iio_device_id, const std::vector<cros::mojom::DeviceType>& types) {
  if (std::find(types.begin(), types.end(),
                cros::mojom::DeviceType::PROXIMITY) == types.end()) {
    // Not a proximity sensor. Ignoring this device.
    return;
  }

  if (sensors_.find(iio_device_id) != sensors_.end()) {
    // Has already added this device.
    return;
  }

  auto& sensor = sensors_[iio_device_id];

  sensor_service_handler_->GetDevice(
      iio_device_id, sensor.remote.BindNewPipeAndPassReceiver());
  sensor.remote.set_disconnect_with_reason_handler(
      base::BindOnce(&UserProximityWatcherMojo::OnSensorDeviceDisconnect,
                     base::Unretained(this), iio_device_id));

  sensor.remote->GetAttributes(
      std::vector<std::string>{cros::mojom::kSysPath, cros::mojom::kDevlink},
      base::BindOnce(&UserProximityWatcherMojo::GetAttributesCallback,
                     base::Unretained(this), iio_device_id));
}

void UserProximityWatcherMojo::SensorServiceConnected() {
  for (auto& sensor : sensors_)
    InitializeSensor(sensor.first);
}

void UserProximityWatcherMojo::SensorServiceDisconnected() {
  ResetSensorService();
}

void UserProximityWatcherMojo::AddObserver(UserProximityObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);

  // Add existing sensor to observer
  for (auto const& sensor : sensors_) {
    observer->OnNewSensor(sensor.first, sensor.second.role);
  }
}

void UserProximityWatcherMojo::RemoveObserver(UserProximityObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void UserProximityWatcherMojo::ResetSensorService() {
  for (auto& sensor : sensors_) {
    sensor.second.remote.reset();
    sensor.second.observer.reset();
  }
}

void UserProximityWatcherMojo::OnSensorDeviceDisconnect(
    int32_t id, uint32_t custom_reason_code, const std::string& description) {
  const auto reason = static_cast<cros::mojom::SensorDeviceDisconnectReason>(
      custom_reason_code);
  LOG(WARNING) << "OnSensorDeviceDisconnect: " << id << ", reason: " << reason
               << ", description: " << description;

  switch (reason) {
    case cros::mojom::SensorDeviceDisconnectReason::IIOSERVICE_CRASHED:
      ResetSensorService();
      break;

    case cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED:
      // This proximity sensor is not in use.
      sensors_.erase(id);
      break;
  }
}

void UserProximityWatcherMojo::GetAttributesCallback(
    int32_t id, const std::vector<std::optional<std::string>>& values) {
  auto& sensor = sensors_[id];
  DCHECK(sensor.remote.is_bound());

  if (values.size() < 2) {
    LOG(ERROR) << "Sensor values doesn't contain the attributes.";
    sensor.ignored = true;
    sensor.remote.reset();
    return;
  }

  if (values.size() != 2) {
    LOG(WARNING) << "Sensor values contain more than the syspath & devlink "
                    "attributes. Size: "
                 << values.size();
  }

  if (values[0].has_value() &&
      values[0]->find("-activity") != std::string::npos) {
    sensor.type = SensorType::ACTIVITY;
    if (use_activity_proximity_for_cellular_)
      sensor.role |= UserProximityObserver::SensorRole::SENSOR_ROLE_LTE;
    if (use_activity_proximity_for_wifi_)
      sensor.role |= UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI;

    // Should only has one index: in_proximity_change_either_en.
    sensor.event_indices.push_back(0);
  } else if (values[1].has_value() &&
             values[1]->find("proximity") != std::string::npos) {
    sensor.type = SensorType::SAR;
    auto sar_config_reader = libsar::SarConfigReader(
        config_.get(), values[1].value(), delegate_.get());
    if (use_proximity_for_cellular_ && sar_config_reader.isCellular())
      sensor.role |= UserProximityObserver::SensorRole::SENSOR_ROLE_LTE;
    if (use_proximity_for_wifi_ && sar_config_reader.isWifi())
      sensor.role |= UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI;

    auto config_dict_opt = sar_config_reader.GetSarConfigDict();
    if (!config_dict_opt.has_value()) {
      LOG(ERROR) << "Sar sensor with id " << id
                 << " doesn't have a valid config dict";
      sensor.ignored = true;
      sensor.remote.reset();
      return;
    }

    const base::Value::Dict& config_dict = config_dict_opt.value();

    const base::Value::List* channel_list =
        config_dict.FindList("channelConfig");
    if (channel_list) {
      // Semtech supports multiple channels, a given observer may received
      // FAR/NEAR message from multiple channels.
      for (const base::Value& channel : *channel_list) {
        const base::Value::Dict& channel_dict = channel.GetDict();
        const std::string* channel_name = channel_dict.FindString("channel");
        if (!channel_name) {
          LOG(ERROR) << "channel identifier required";
          continue;
        }

        int channel_int;
        if (!base::StringToInt(*channel_name, &channel_int)) {
          LOG(ERROR) << "Invalid channel_name: " << channel_name;
          continue;
        }

        sensor.event_indices.push_back(channel_int);
      }
    }

    if (sensor.event_indices.empty()) {
      LOG(ERROR) << "Sar sensor with id " << id
                 << " doesn't have any event index enabled";
      sensor.ignored = true;
      sensor.remote.reset();
      return;
    }
  }

  if (sensor.role == UserProximityObserver::SensorRole::SENSOR_ROLE_NONE) {
    LOG(INFO) << "Sensor with id " << id << " not usable for any subsystem";
    sensor.ignored = true;
    sensor.remote.reset();
    return;
  }

  for (auto& observer : observers_)
    observer.OnNewSensor(id, sensor.role);

  InitializeSensor(id);
}

void UserProximityWatcherMojo::InitializeSensor(int32_t id) {
  auto& sensor = sensors_[id];

  if (sensor.ignored)
    return;

  if (!sensor.remote.is_bound()) {
    sensor_service_handler_->GetDevice(
        id, sensor.remote.BindNewPipeAndPassReceiver());
    sensor.remote.set_disconnect_with_reason_handler(
        base::BindOnce(&UserProximityWatcherMojo::OnSensorDeviceDisconnect,
                       base::Unretained(this), id));
  }

  sensor.observer = std::make_unique<ProximityEventsObserver>(
      id, sensor.event_indices, std::move(sensor.remote), &observers_);
}

}  // namespace system
}  // namespace power_manager

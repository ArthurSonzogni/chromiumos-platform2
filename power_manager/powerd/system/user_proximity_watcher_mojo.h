// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_USER_PROXIMITY_WATCHER_MOJO_H_
#define POWER_MANAGER_POWERD_SYSTEM_USER_PROXIMITY_WATCHER_MOJO_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/observer_list.h>

#include <cros_config/cros_config.h>
#include <libsar/sar_config_reader.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/proximity_events_observer.h"
#include "power_manager/powerd/system/sensor_service_handler.h"
#include "power_manager/powerd/system/sensor_service_handler_observer.h"
#include "power_manager/powerd/system/user_proximity_watcher_interface.h"

namespace power_manager {

class PrefsInterface;

namespace system {

class UserProximityObserver;

// Concrete implementation of UserProximityWatcherInterface: detects proximity
// sensors and reports proximity events.
class UserProximityWatcherMojo : public UserProximityWatcherInterface,
                                 public SensorServiceHandlerObserver {
 public:
  // Sensor type for proximity detection.
  enum class SensorType { UNKNOWN, SAR, ACTIVITY };

  explicit UserProximityWatcherMojo(
      PrefsInterface* prefs,
      std::unique_ptr<brillo::CrosConfigInterface> config,
      std::unique_ptr<libsar::SarConfigReader::Delegate> delegate,
      TabletMode tablet_mode,
      SensorServiceHandler* sensor_service_handler);

  UserProximityWatcherMojo(const UserProximityWatcherMojo&) = delete;
  UserProximityWatcherMojo& operator=(const UserProximityWatcherMojo&) = delete;

  ~UserProximityWatcherMojo() override;

  // UserProximityWatcherInterface implementation:
  void AddObserver(UserProximityObserver* observer) override;
  void RemoveObserver(UserProximityObserver* observer) override;
  // Called when the tablet mode changes.
  void HandleTabletModeChange(TabletMode mode) override;

  // SensorServiceHandlerObserver implementation:
  void OnNewDeviceAdded(
      int32_t iio_device_id,
      const std::vector<cros::mojom::DeviceType>& types) override;
  void SensorServiceConnected() override;
  void SensorServiceDisconnected() override;

 private:
  struct SensorInfo {
    // Something is wrong of the attributes, or this proximity sensor is not
    // needed.
    bool ignored = false;

    SensorType type;
    // Bitwise combination of UserProximityObserver::SensorRole values
    uint32_t role;

    std::vector<int> event_indices;

    // Temporarily stores the proximity mojo::Remote, waiting for its attribute
    // information. It'll be passed to ProximityObserverMojo as an argument
    // after all information is collected.
    mojo::Remote<cros::mojom::SensorDevice> remote;

    std::unique_ptr<ProximityEventsObserver> observer;
  };

  // Opens a file descriptor suitable for listening to proximity events for
  // the sensor at |devlink|, and notifies registered observers that a new
  // valid proximity sensor exists.
  bool OnSensorDetected(const SensorType type,
                        const std::string& syspath,
                        const std::string& devlink);

  void ResetSensorService();

  void OnSensorDeviceDisconnect(int32_t id,
                                uint32_t custom_reason_code,
                                const std::string& description);

  void GetAttributesCallback(
      int32_t id, const std::vector<std::optional<std::string>>& values);

  void InitializeSensor(int32_t id);

  std::unique_ptr<brillo::CrosConfigInterface> config_;
  std::unique_ptr<libsar::SarConfigReader::Delegate> delegate_;

  TabletMode tablet_mode_ = TabletMode::UNSUPPORTED;
  base::ObserverList<UserProximityObserver> observers_;

  // Mapping between IIO event file descriptors and sensor details.
  std::unordered_map<int, SensorInfo> sensors_;

  bool use_proximity_for_cellular_ = false;
  bool use_proximity_for_wifi_ = false;
  bool use_activity_proximity_for_cellular_ = false;
  bool use_activity_proximity_for_wifi_ = false;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_USER_PROXIMITY_WATCHER_MOJO_H_

// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_H_
#define POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/message_loop/message_loop.h>
#include <base/observer_list.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/sar_watcher_interface.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {

class PrefsInterface;

namespace system {

class SarObserver;
struct UdevEvent;
class UdevInterface;

// Concrete implementation of SarWatcherInterface: detects proximity
// sensors and reports proximity events.
class SarWatcher : public SarWatcherInterface,
                   public UdevSubsystemObserver,
                   public base::MessageLoopForIO::Watcher {
 public:
  // Defines which subsystem(s) a sensor can provide proximity data for.
  enum SensorRole {
    SENSOR_ROLE_NONE = 0,
    SENSOR_ROLE_WIFI = 1u << 0,
    SENSOR_ROLE_LTE = 1u << 1,
  };

  // udev subsystem to watch.
  static const char kIioUdevSubsystem[];

  // udev device type.
  static const char kIioUdevDevice[];

  // Mechanism to obtain a file handle suitable for observing IIO events
  using OpenIioEventsFunc = base::Callback<int(const base::FilePath&)>;

  void set_open_iio_events_func_for_testing(OpenIioEventsFunc f);

  SarWatcher();
  ~SarWatcher() override;

  // Returns true on success.
  bool Init(PrefsInterface* prefs, UdevInterface* udev);

  // SarWatcherInterface implementation:
  void AddObserver(SarObserver* observer) override;
  void RemoveObserver(SarObserver* observer) override;

  // UdevSubsystemObserver implementation:
  void OnUdevEvent(const UdevEvent& event) override;

  // Watcher implementation:
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override {}

 private:
  struct SensorInfo {
    std::string syspath;
    std::string devlink;
    int event_fd;
    // Bitwise combination of SensorRole values
    uint32_t role;
    std::unique_ptr<base::MessageLoopForIO::FileDescriptorWatcher> watcher;
  };

  // Determines whether |dev| represents a proximity sensor connected via
  // the IIO subsystem. If so, |devlink_out| is the path to the file to be used
  // to read proximity events from this device.
  bool IsIioProximitySensor(const UdevDeviceInfo& dev,
                            std::string* devlink_out);

  // Opens a file descriptor suitable for listening to proximity events for
  // the sensor at |devlink|, and notifies registered observers that a new
  // valid proximity sensor exists.
  bool OnSensorDetected(const std::string& syspath, const std::string& devlink);

  OpenIioEventsFunc open_iio_events_func_;

  UdevInterface* udev_ = nullptr;  // non-owned
  base::ObserverList<SarObserver> observers_;

  // Mapping between IIO event file descriptors and sensor details.
  std::unordered_map<int, SensorInfo> sensors_;

  DISALLOW_COPY_AND_ASSIGN(SarWatcher);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_H_

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_MONITOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_MONITOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"

namespace diagnostics {

class EvdevMonitor {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // Check if |dev| is the target device.
    virtual bool IsTarget(LibevdevWrapper* dev) = 0;
    // Deal with the events and report to the caller through observer.
    virtual void FireEvent(const input_event& event, LibevdevWrapper* dev) = 0;
    // Initialization fail. Delegate should reset the observer.
    virtual void InitializationFail(uint32_t custom_reason,
                                    const std::string& description) = 0;
    // Collect properties here and report to the caller through observer.
    virtual void ReportProperties(LibevdevWrapper* dev) = 0;
  };

  // This class manages the life cycle of an opened evdev node.
  class EvdevDevice {
   public:
    EvdevDevice(base::ScopedFD fd, std::unique_ptr<LibevdevWrapper> dev);
    EvdevDevice(const EvdevDevice& device) = delete;
    EvdevDevice(EvdevDevice&& device) = delete;
    virtual ~EvdevDevice();

    // Starts watching the readable state of |fd_| and calls |on_evdev_event|
    // when |fd_| is readable. Returns whether the monitoring starts
    // successfully.
    bool StarWatchingEvents(
        base::RepeatingCallback<void(LibevdevWrapper*)> on_evdev_event);

   private:
    // The fd of opened evdev node.
    base::ScopedFD fd_;
    // The libevdev device object.
    std::unique_ptr<LibevdevWrapper> dev_;
    // The watcher to monitor if the |fd_| is readable.
    std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  };

  explicit EvdevMonitor(std::unique_ptr<Delegate> delegate);
  EvdevMonitor(const EvdevMonitor& oth) = delete;
  EvdevMonitor(EvdevMonitor&& oth) = delete;
  virtual ~EvdevMonitor();

  // Starts monitoring evdev events.
  //
  // If `allow_multiple_devices` is true, all evdev nodes for which
  // `Delegate::IsTarget` returns true will be monitored. Otherwise, at most one
  // evdev node will be monitored.
  void StartMonitoring(bool allow_multiple_devices);

 protected:
  // Creates a libevdev device object from `fd`.
  // Declared as virtual to be overridden for testing.
  virtual std::unique_ptr<LibevdevWrapper> CreateLibevdev(int fd);

 private:
  // Monitors the evdev device created from `path` and returns whether the
  // monitoring was successful or not.
  bool TryMonitoringEvdevDevice(const base::FilePath& path);

  // Called when the fd of a targeted evdev device |dev| is readable. It reads
  // events from the fd and fires events through |FireEvent|.
  void OnEvdevEvent(LibevdevWrapper* dev);

  // The evdev devices to monitor.
  std::vector<std::unique_ptr<EvdevDevice>> devs_;
  // Delegate to implement dedicated behaviors for different evdev devices.
  std::unique_ptr<Delegate> delegate_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_MONITOR_H_

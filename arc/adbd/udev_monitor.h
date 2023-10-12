// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_ADBD_UDEV_MONITOR_H_
#define ARC_ADBD_UDEV_MONITOR_H_

#include <libudev.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_monitor.h>
#include <base/threading/thread.h>

namespace adbd {

// Class to monitor typec usb events and update usb_role for each
// of the added devices. This helps setup the usb host-to-host mode
// connection required for DbC.
class UdevMonitor {
 public:
  UdevMonitor();
  ~UdevMonitor() = default;

  // Set up monitoring.
  bool Init();

 private:
  // Udev monitor thread.
  base::Thread udev_thread_;
  std::unique_ptr<brillo::Udev> udev_;
  std::unique_ptr<brillo::UdevMonitor> udev_monitor_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      udev_monitor_watcher_;

  // Start monitoring for usb events.
  void StartWatching(int fd);
  // Enumerate existing devices.
  bool Enumerate();
  // Callback when typec event gets triggered.
  void OnUdevEvent();
  // Handle device add events.
  void OnDeviceAdd(const base::FilePath& path);
};

}  // namespace adbd

#endif  // ARC_ADBD_UDEV_MONITOR_H_

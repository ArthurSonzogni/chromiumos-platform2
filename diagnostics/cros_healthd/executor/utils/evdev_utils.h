// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_EVDEV_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_EVDEV_UTILS_H_

#include <libevdev/libevdev.h>
#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

namespace diagnostics {

class EvdevUtil {
 public:
  class Delegate {
   public:
    // Check if |dev| is the target device.
    virtual bool IsTarget(libevdev* dev) = 0;
    // Deal with the events and report to the caller through observer.
    virtual void FireEvent(const input_event& event, libevdev* dev) = 0;
    // Initialization fail. Delegate should reset the observer.
    virtual void InitializationFail() = 0;
    // Collect properties here and report to the caller through observer.
    virtual void ReportProperties(libevdev* dev) = 0;
  };

  explicit EvdevUtil(Delegate* delegate);
  EvdevUtil(const EvdevUtil& oth) = delete;
  EvdevUtil(EvdevUtil&& oth) = delete;
  virtual ~EvdevUtil();

 private:
  void Initialize();
  bool Initialize(const base::FilePath& path);

  // When |fd_| is readable, it reads event from it and tries to fire event
  // through |FireEvent|.
  void OnEvdevEvent();

  // The fd of opened evdev node.
  base::ScopedFD fd_;
  // The watcher to monitor if the |fd_| is readable.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  // The libevdev device object.
  libevdev* dev_;
  // Delegate to implement dedicated behaviors for different evdev devices.
  Delegate* const delegate_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_EVDEV_UTILS_H_

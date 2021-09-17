// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_CROS_DISKS_CLIENT_H_
#define RMAD_SYSTEM_CROS_DISKS_CLIENT_H_

#include <string>
#include <vector>

#include <base/callback.h>

namespace rmad {

// Simplified return values from cros-disks.
struct DeviceProperties {
  std::string device_file;
  bool is_on_removable_device;
  bool is_auto_mountable;
};

struct MountEntry {
  bool success;
  std::string source;
  std::string mount_path;
};

class CrosDisksClient {
 public:
  CrosDisksClient() = default;
  virtual ~CrosDisksClient() = default;

  virtual bool EnumerateDevices(std::vector<std::string>* devices) = 0;
  virtual bool GetDeviceProperties(const std::string& device,
                                   DeviceProperties* device_properties) = 0;

  // cros-disks Mount method doesn't reply anything. It sends out a
  // |MountCompleted| signal once the mount finishes so we need a handler to
  // catch the signal. On the other hand, Unmount method sends back a reply
  // directly.
  virtual void AddMountCompletedHandler(
      base::RepeatingCallback<void(const MountEntry&)> callback) = 0;
  virtual void Mount(const std::string& source,
                     const std::string& filesystem_type,
                     const std::vector<std::string>& options) = 0;
  virtual bool Unmount(const std::string& path,
                       const std::vector<std::string>& options,
                       uint32_t* result) = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_CROS_DISKS_CLIENT_H_

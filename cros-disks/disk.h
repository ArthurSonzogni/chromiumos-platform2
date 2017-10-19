// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_DISK_H_
#define CROS_DISKS_DISK_H_

#include <stdint.h>

#include <string>
#include <vector>

#include <chromeos/dbus/service_constants.h>

namespace cros_disks {

// A simple type that describes a storage device attached to our system.
//
// This class was designed to run in a single threaded context and should not
// be considered thread safe.
struct Disk {
 public:
  Disk();
  ~Disk() = default;

  // Returns a presentation name of the disk, which can be used to name
  // the mount directory of the disk. The naming scheme is as follows:
  // (1) Use a non-empty label if the disk has one.
  // (2) Otherwise, use one of the following names based on the device
  //     media type:
  //     - USB drive
  //     - SD card
  //     - Optical disc
  //     - Mobile device
  //     - External drive (if the device media type is unknown)
  // Any forward slash '/' in the presentation name is replaced with an
  // underscore '_'.
  std::string GetPresentationName() const;

  bool IsMounted() const { return !mount_paths.empty(); }

  bool IsOpticalDisk() const {
    return (media_type == DEVICE_MEDIA_OPTICAL_DISC ||
            media_type == DEVICE_MEDIA_DVD);
  }

  bool is_drive;
  bool is_hidden;
  bool is_auto_mountable;
  bool is_media_available;
  bool is_on_boot_device;
  bool is_on_removable_device;
  bool is_rotational;
  bool is_read_only;
  bool is_virtual;
  std::vector<std::string> mount_paths;
  std::string native_path;
  std::string device_file;
  std::string filesystem_type;
  std::string uuid;
  std::string label;
  std::string vendor_id;
  std::string vendor_name;
  std::string product_id;
  std::string product_name;
  std::string drive_model;
  DeviceMediaType media_type;
  uint64_t device_capacity;
  uint64_t bytes_remaining;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_DISK_H_

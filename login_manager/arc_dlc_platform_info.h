// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_DLC_PLATFORM_INFO_H_
#define LOGIN_MANAGER_ARC_DLC_PLATFORM_INFO_H_

#include <optional>
#include <string>

#include <base/byte_count.h>
#include <base/files/file_path.h>

namespace login_manager {

// An abstract interface for retrieving disk information
// from the platform, which is required for ARC DLC hardware filter.
class ArcDlcPlatformInfo {
 public:
  virtual ~ArcDlcPlatformInfo() = default;

  // Returns the physical device name underlying the root partition.
  // Returns std::nullopt on failure.
  virtual std::optional<std::string> GetRootDeviceName() = 0;

  // Returns the size of the block device in bytes.
  // Returns std::nullopt on failure.
  virtual std::optional<base::ByteCount> GetDeviceSize(
      const base::FilePath& dev_path) = 0;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_DLC_PLATFORM_INFO_H_

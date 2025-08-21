// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_DLC_PLATFORM_INFO_IMPL_H_
#define LOGIN_MANAGER_ARC_DLC_PLATFORM_INFO_IMPL_H_

#include <optional>
#include <string>

#include <base/byte_count.h>
#include <base/files/file_path.h>

#include "login_manager/arc_dlc_platform_info.h"

namespace login_manager {

// A concrete implementation of ArcDlcPlatformInfo.
class ArcDlcPlatformInfoImpl : public ArcDlcPlatformInfo {
 public:
  ArcDlcPlatformInfoImpl() = default;
  ArcDlcPlatformInfoImpl(const ArcDlcPlatformInfoImpl&) = delete;
  ArcDlcPlatformInfoImpl& operator=(const ArcDlcPlatformInfoImpl&) = delete;
  ~ArcDlcPlatformInfoImpl() override = default;

  // ArcDlcPlatformInfo overrides:
  std::optional<std::string> GetRootDeviceName() override;
  std::optional<base::ByteCount> GetDeviceSize(
      const base::FilePath& dev_path) override;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_DLC_PLATFORM_INFO_IMPL_H_

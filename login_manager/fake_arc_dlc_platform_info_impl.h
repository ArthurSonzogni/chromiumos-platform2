// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_FAKE_ARC_DLC_PLATFORM_INFO_IMPL_H_
#define LOGIN_MANAGER_FAKE_ARC_DLC_PLATFORM_INFO_IMPL_H_

#include <optional>
#include <string>
#include <utility>

#include <base/byte_count.h>
#include <base/files/file_path.h>

#include "login_manager/arc_dlc_platform_info.h"

namespace login_manager {

// A fake implementation of ArcDlcHardwareFilterHelper for testing.
class FakeArcDlcPlatformInfoImpl : public ArcDlcPlatformInfo {
 public:
  FakeArcDlcPlatformInfoImpl() = default;
  FakeArcDlcPlatformInfoImpl(const FakeArcDlcPlatformInfoImpl&) = delete;
  FakeArcDlcPlatformInfoImpl& operator=(const FakeArcDlcPlatformInfoImpl&) =
      delete;
  ~FakeArcDlcPlatformInfoImpl() override = default;

  std::optional<std::string> GetRootDeviceName() override {
    return root_device_name_;
  }

  std::optional<base::ByteCount> GetDeviceSize(
      const base::FilePath& dev_path) override {
    return device_size_bytes_;
  }

  // Setters to control the behavior of the fake class.
  void set_root_device_name(std::optional<std::string> name) {
    root_device_name_ = std::move(name);
  }

  void set_device_size_bytes(std::optional<base::ByteCount> size) {
    device_size_bytes_ = std::move(size);
  }

 private:
  std::optional<std::string> root_device_name_;
  std::optional<base::ByteCount> device_size_bytes_;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_FAKE_ARC_DLC_PLATFORM_INFO_IMPL_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DISK_USAGE_PROXY_H_
#define SPACED_DISK_USAGE_PROXY_H_

#include <memory>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include "spaced/dbus-proxies.h"
#include "spaced/disk_usage.h"

namespace spaced {

class BRILLO_EXPORT DiskUsageProxy : public DiskUsageUtil {
 public:
  DiskUsageProxy();
  ~DiskUsageProxy() override = default;

  int64_t GetFreeDiskSpace(const base::FilePath& path) override;
  int64_t GetTotalDiskSpace(const base::FilePath& path) override;
  int64_t GetRootDeviceSize() override;

 private:
  std::unique_ptr<org::chromium::SpacedProxy> spaced_proxy_;
};

}  // namespace spaced

#endif  // SPACED_DISK_USAGE_PROXY_H_

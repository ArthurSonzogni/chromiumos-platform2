// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// spaced_cli provides a command line interface disk usage queries.

#include <iostream>

#include <base/files/file_path.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "spaced/disk_usage_proxy.h"

int main(int argc, char** argv) {
  DEFINE_string(get_free_disk_space, "",
                "Gets free disk space available on the given path");
  DEFINE_string(get_total_disk_space, "",
                "Gets total disk space available on the given path");
  DEFINE_bool(get_root_device_size, false, "Gets the size of the root device");

  brillo::FlagHelper::Init(argc, argv, "Chromium OS Space Daemon CLI");

  std::unique_ptr<spaced::DiskUsageProxy> disk_usage_proxy =
      spaced::DiskUsageProxy::Generate();

  if (!disk_usage_proxy) {
    LOG(ERROR) << "Failed to get disk usage proxy";
    return 1;
  }

  if (!FLAGS_get_free_disk_space.empty()) {
    std::cout << disk_usage_proxy->GetFreeDiskSpace(
        base::FilePath(FLAGS_get_free_disk_space));
    return 0;
  } else if (!FLAGS_get_total_disk_space.empty()) {
    std::cout << disk_usage_proxy->GetTotalDiskSpace(
        base::FilePath(FLAGS_get_total_disk_space));
    return 0;
  } else if (FLAGS_get_root_device_size) {
    std::cout << disk_usage_proxy->GetRootDeviceSize();
    return 0;
  }

  return 1;
}

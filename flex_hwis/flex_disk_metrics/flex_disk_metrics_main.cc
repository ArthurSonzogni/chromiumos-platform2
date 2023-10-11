// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_disk_metrics/flex_disk_metrics.h"

#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <rootdev/rootdev.h>

namespace {

// Get the name of the disk device the OS is running on (e.g. "sda").
std::optional<std::string> GetRootDiskDeviceName() {
  char path[PATH_MAX];
  const auto ret = rootdev(path, PATH_MAX, /*full=*/true, /*strip=*/true);
  if (ret == 0) {
    const base::FilePath disk_path(path);
    return disk_path.BaseName().value();
  } else {
    LOG(ERROR) << "Failed to get root device, error=" << ret;
    return std::nullopt;
  }
}

}  // namespace

int main() {
  brillo::InitLog(brillo::kLogToStderr | brillo::kLogToSyslog);

  const auto root_disk_device_name = GetRootDiskDeviceName();
  if (!root_disk_device_name) {
    return EXIT_FAILURE;
  }

  // The list of partition labels must match the variants of the
  // `Platform.FlexPartitionSize.{Partition}` histogram:
  // https://source.chromium.org/chromium/chromium/src/+/HEAD:tools/metrics/histograms/metadata/platform/histograms.xml
  std::vector<std::string> partition_labels = {
      "EFI-SYSTEM", "KERN-A", "KERN-B", "ROOT-A", "ROOT-B",
  };

  MetricsLibrary metrics;

  const MapPartitionLabelToMiBSize label_to_size_map =
      GetPartitionSizeMap(base::FilePath("/"), root_disk_device_name.value());

  if (SendDiskMetrics(metrics, label_to_size_map, partition_labels)) {
    return EXIT_SUCCESS;
  } else {
    return EXIT_FAILURE;
  }
}

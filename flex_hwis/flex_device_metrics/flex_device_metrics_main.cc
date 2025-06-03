// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <rootdev/rootdev.h>

#include "flex_hwis/flex_device_metrics/flex_device_metrics.h"

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

// Get the size of a set of partitions and send as UMAs.
//
// Returns true on success, false if any error occurs.
bool GatherAndSendDiskMetrics(MetricsLibraryInterface& metrics) {
  const auto root_disk_device_name = GetRootDiskDeviceName();
  if (!root_disk_device_name) {
    return false;
  }

  // The list of partition labels must match the variants of the
  // `Platform.FlexPartitionSize.{Partition}` histogram:
  // https://source.chromium.org/chromium/chromium/src/+/HEAD:tools/metrics/histograms/metadata/platform/histograms.xml
  std::vector<std::string> partition_labels = {
      "EFI-SYSTEM", "KERN-A", "KERN-B", "ROOT-A", "ROOT-B",
  };

  const MapPartitionLabelToMiBSize label_to_size_map =
      GetPartitionSizeMap(base::FilePath("/"), root_disk_device_name.value());

  if (!SendDiskMetrics(metrics, label_to_size_map, partition_labels)) {
    return false;
  }

  return true;
}

}  // namespace

int main() {
  brillo::InitLog(brillo::kLogToStderr | brillo::kLogToSyslog);

  MetricsLibrary metrics;

  int rc = EXIT_SUCCESS;

  if (!GatherAndSendDiskMetrics(metrics)) {
    rc = EXIT_FAILURE;
  }

  if (!SendCpuIsaLevelMetric(metrics, GetCpuIsaLevel())) {
    rc = EXIT_FAILURE;
  }

  if (!SendBootMethodMetric(metrics, GetBootMethod(base::FilePath("/")))) {
    rc = EXIT_FAILURE;
  }

  if (ShouldSendFlexorInstallMetric(base::FilePath("/"))) {
    if (!SendFlexorInstallMetric(metrics)) {
      rc = EXIT_FAILURE;
    }
  }

  return rc;
}

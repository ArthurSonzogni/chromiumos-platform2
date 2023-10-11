// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_DISK_METRICS_FLEX_DISK_METRICS_H_
#define FLEX_HWIS_FLEX_DISK_METRICS_FLEX_DISK_METRICS_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <metrics/metrics_library.h>

// Convert from 512-byte disk blocks to MiB. Round down if the size is
// not an even MiB value.
int ConvertBlocksToMiB(int num_blocks);

// Get a partition's label from the `uevent` file in the partition's
// directory under `/sys`.
//
// Arguments:
//   sys_partition_path: Path of a partition directory under /sys.
//                       For example: /sys/class/block/sda/sda2.
//
// Returns the partition's label on success, for example
// "KERN-A". Returns `nullopt` if any error occurs.
std::optional<std::string> GetPartitionLabelFromUevent(
    const base::FilePath& sys_partition_path);

// Get a partition's size in MiB from the `size` file in the partition's
// directory under `/sys`.
//
// Arguments:
//   sys_partition_path: Path of a partition directory under /sys.
//                       For example: /sys/class/block/sda/sda2.
//
// Returns the partition's size in MiB on success, rounded down if
// necessary. Returns `nullopt` if any error occurs.
std::optional<int> GetPartitionSizeInMiB(
    const base::FilePath& sys_partition_path);

// Map from partition label to partition size in MiB. A label may have
// more than one entry since partition labels are not guaranteed to be
// unique.
using MapPartitionLabelToMiBSize = std::multimap<std::string, int>;

// Create a map from partition label to partition size in MiB.
//
// This looks at files in `sys` to get partition info. For example:
// /sys/class/block/sda/
//   -> sda2/
//     -> File `uevent` contains the line "PARTNAME=KERN-A"
//     -> File `size` contains "131072"
//
// Why not use /dev/disk/by-partlabel? There's no defined handling for
// duplicate partition names. An example problem this could cause: a
// user could run Flex from a hard drive, but also have a Flex USB
// installer attached. Both disks would have the same partition names,
// but with different sizes. The by-partlabel directory could contain
// links to either one.
//
// Why not use cgpt? That requires read access to block files under
// /dev. That could be done by running under a user in the "disk" group,
// but doing it without cgpt allows the program to run under a more
// restricted user.
//
// Arguments:
//   root: Path of the filesystem root where `sys` is mounted.
//         Normally this is just `/`, but can be changed for testing.
//   root_disk_device_name: Name of the root disk device. Example: "sda".
//
// Returns a multimap with all partitions for which the size was
// successfully retrieved. A multimap is used because some partitions
// may have the same label, e.g. "reserved".
MapPartitionLabelToMiBSize GetPartitionSizeMap(
    const base::FilePath& root, std::string_view root_disk_device_name);

// Send a sparse metric for the size of each partition in the
// `partition_label` vector.
//
// A sparse metric is used because we want to know exact values. Only a
// few values are actually expected (e.g. the kernel partition should
// always be either 16MiB or 64MiB), but any value is possible.
//
// Partition sizes are read from the `label_to_size_map` multimap. If a
// partition is missing from that map, or if it has multiple entries,
// it's treated as an error.
//
// An error in sending one metric does not prevent other metrics from
// being sent.
//
// Arguments:
//   metrics: Interface to the metrics library.
//   label_to_size_map: Multimap created by `GetPartitionSizeMap`.
//   partition_labels: Vector of partition names to send metrics for.
//
// Returns true on success, false if any error occurs.
bool SendDiskMetrics(MetricsLibraryInterface& metrics,
                     const MapPartitionLabelToMiBSize& label_to_size_map,
                     const std::vector<std::string>& partition_labels);

#endif  // FLEX_HWIS_FLEX_DISK_METRICS_FLEX_DISK_METRICS_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_disk_metrics/flex_disk_metrics.h"

#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

int ConvertBlocksToMiB(int num_blocks) {
  const int bytes_per_block = 512;
  const int bytes_per_mib = 1024 * 1024;
  return (num_blocks * bytes_per_block) / bytes_per_mib;
}

std::optional<std::string> GetPartitionLabelFromUevent(
    const base::FilePath& sys_partition_path) {
  const auto uevent_path = sys_partition_path.Append("uevent");
  std::string uevent;
  if (!ReadFileToStringWithMaxSize(uevent_path, &uevent,
                                   /*max_size=*/4096)) {
    PLOG(ERROR) << "Failed to read " << uevent_path;
    return std::nullopt;
  }

  for (const auto& line : base::SplitStringPiece(
           uevent, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const auto index = line.find_first_of('=');
    if (index == std::string::npos) {
      continue;
    }

    const auto& key = line.substr(0, index);
    const auto& val = line.substr(index + 1);

    if (key == "PARTNAME") {
      return std::string(val);
    }
  }

  return std::nullopt;
}

std::optional<int> GetPartitionSizeInMiB(
    const base::FilePath& sys_partition_path) {
  const auto size_path = sys_partition_path.Append("size");
  std::string size_str;
  if (!ReadFileToStringWithMaxSize(size_path, &size_str,
                                   /*max_size=*/32)) {
    PLOG(ERROR) << "Failed to read " << size_path;
    return std::nullopt;
  }

  const auto size_str_trimmed =
      TrimWhitespaceASCII(size_str, base::TRIM_TRAILING);

  int partition_size_in_blocks = 0;
  if (!base::StringToInt(size_str_trimmed, &partition_size_in_blocks)) {
    LOG(ERROR) << "TODO: conversion error";
    return std::nullopt;
  }

  return ConvertBlocksToMiB(partition_size_in_blocks);
}

MapPartitionLabelToMiBSize GetPartitionSizeMap(
    const base::FilePath& root, std::string_view root_disk_device_name) {
  MapPartitionLabelToMiBSize label_to_size_map;

  const auto sys_block_root_path =
      root.Append("sys/block").Append(root_disk_device_name);
  base::FileEnumerator enumerator(sys_block_root_path, /*recursive=*/false,
                                  base::FileEnumerator::DIRECTORIES);

  for (base::FilePath subdir = enumerator.Next(); !subdir.empty();
       subdir = enumerator.Next()) {
    // Ignore directories that don't look like partitions.
    if (!subdir.BaseName().value().starts_with(root_disk_device_name)) {
      continue;
    }

    // Get the partition label, e.g. "EFI-SYSTEM".
    const auto partition_label = GetPartitionLabelFromUevent(subdir);
    if (!partition_label.has_value()) {
      continue;
    }

    // Get the partition's size in MiB.
    const auto partition_size_in_mib = GetPartitionSizeInMiB(subdir);
    if (!partition_size_in_mib.has_value()) {
      continue;
    }

    label_to_size_map.insert(
        std::make_pair(partition_label.value(), partition_size_in_mib.value()));
  }

  return label_to_size_map;
}

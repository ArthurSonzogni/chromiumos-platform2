// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/log_store_manifest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace minios {

const uint64_t kLogStoreMagic = 0x13577531;
const uint64_t kBlockSize = 512;
// Offset from end of partition. Location for storing manifest.
const uint64_t kDefaultManifestStoreOffset = 2;

LogStoreManifest::LogStoreManifest(base::FilePath disk_path,
                                   uint64_t kernel_size,
                                   uint64_t partition_size)
    : disk_path_(disk_path),
      disk_(disk_path,
            base::File::FLAG_OPEN | base::File::FLAG_WRITE |
                base::File::FLAG_READ),
      kernel_size_(kernel_size),
      partition_size_(partition_size),
      manifest_store_start_(partition_size_ -
                            (kDefaultManifestStoreOffset * kBlockSize)) {
  // Verify sanity of kernel size. Kernel MUST end before manifest blocks to
  // avoid corruption. In case of invalid offset, set valid flag to false to
  // disable reading and writing later on.
  if (kernel_size_ > manifest_store_start_) {
    LOG(ERROR) << base::StringPrintf(
        "Invalid kernel size, disabling manifest storage. kernel_size: %ld, "
        "partition_size: %ld, "
        "manifest_store_location: %ld",
        kernel_size_, partition_size_, manifest_store_start_);
    SetValid(false);
  }

  if (partition_size % kBlockSize != 0) {
    LOG(ERROR) << "Partition is not block aligned, disabling storage. "
                  "Partition size: "
               << partition_size_;

    SetValid(false);
  }

  if (disk_path_.empty()) {
    LOG(ERROR) << "Disabling manifest storage due to empty disk path";
    SetValid(false);
  }

  if (!disk_.IsValid()) {
    LOG(ERROR) << "Failed to open disk to write to: " << disk_path_;
    SetValid(false);
  }
}

bool LogStoreManifest::Generate(const LogManifest::Entry& entry) {
  if (!IsValid()) {
    LOG(ERROR) << "Ignoring manifest generate due to bad params.";
    return false;
  }

  // Fill out manifest and store it for a future write.
  manifest_.emplace();
  auto* log_entry = manifest_->mutable_entry();
  log_entry->CopyFrom(entry);
  return true;
}

std::optional<LogManifest> LogStoreManifest::Retreive() {
  if (!IsValid()) {
    LOG(ERROR) << "Ignoring manifest retrieve due to bad params.";
    return std::nullopt;
  }

  disk_manifest_location_ = FindManifestMagic();

  if (disk_manifest_location_) {
    // Skip ahead size of Magic to reach the serialized manifest.
    disk_.Seek(base::File::FROM_BEGIN,
               disk_manifest_location_.value() + sizeof(kLogStoreMagic));
    LogManifest manifest;
    std::string serialized_manifest;
    auto max_manifest_size =
        partition_size_ -
        (disk_manifest_location_.value() + sizeof(kLogStoreMagic));
    serialized_manifest.resize(max_manifest_size);
    disk_.ReadAtCurrentPos(serialized_manifest.data(), max_manifest_size);
    manifest.ParseFromString(serialized_manifest);
    return manifest;
  }
  LOG(INFO) << "No manifest found on disk.";
  return std::nullopt;
}

bool LogStoreManifest::Write() {
  if (!IsValid()) {
    LOG(ERROR) << "Ignoring manifest write due to bad params.";
    return false;
  }
  if (!manifest_) {
    LOG(ERROR) << "Log store manifest has not been generated!";
    return false;
  }
  // If manifest had been generated, write it to disk.
  disk_.Seek(base::File::FROM_BEGIN, manifest_store_start_);
  // Write magic block header.
  disk_.WriteAtCurrentPos(reinterpret_cast<const char*>(&kLogStoreMagic),
                          sizeof(kLogStoreMagic));

  auto serialized_manifest = manifest_->SerializeAsString();
  disk_.WriteAtCurrentPos(serialized_manifest.c_str(),
                          serialized_manifest.size());
  // Flush and close disk stream.
  if (!disk_.Flush()) {
    LOG(ERROR) << "Failed to flush manifest to device: " << disk_path_
               << " error: " << disk_.GetLastFileError();
    return false;
  }

  return true;
}

void LogStoreManifest::Clear() {
  if (!IsValid()) {
    LOG(ERROR) << "Ignoring manifest clear due to bad params.";
    return;
  }

  if (!disk_manifest_location_) {
    // If a manifest location isn't set, search the partition for a manifest.
    disk_manifest_location_ = FindManifestMagic();
  }

  if (disk_manifest_location_) {
    if (disk_manifest_location_.value() < kernel_size_) {
      LOG(ERROR) << "Manifest found in kernel data, skipping erase";
      return;
    }
    disk_.Seek(base::File::FROM_BEGIN, disk_manifest_location_.value());
  } else {
    // No manifest on disk, return without doing anything.
    return;
  }
  // Clear out all data until the end of the partition.
  std::array<char, kBlockSize> zeros{0};
  auto bytes_to_write = partition_size_ - disk_manifest_location_.value();
  while (bytes_to_write > 0) {
    disk_.WriteAtCurrentPos(zeros.data(), std::min(kBlockSize, bytes_to_write));
    if (bytes_to_write <= kBlockSize)
      break;
    bytes_to_write -= kBlockSize;
  }

  // Flush disk stream.
  if (!disk_.Flush()) {
    LOG(ERROR) << "Failed to clear manifest on device: " << disk_path_
               << " error: " << disk_.GetLastFileError();
  }
  // Clear manifest location since now there's nothing on disk.
  disk_manifest_location_.reset();
}

std::optional<uint64_t> LogStoreManifest::FindManifestMagic() {
  if (!IsValid()) {
    LOG(ERROR) << "Invalid disk to find manifest: " << disk_path_;
    return std::nullopt;
  }

  // Seek to the end of the partition and step backwards the blocks until we
  // find the expected header magic.
  const auto num_blocks = partition_size_ / kBlockSize;
  const auto last_kernel_block =
      ((kernel_size_ + kBlockSize - 1) / kBlockSize) + 1;
  for (uint64_t block = num_blocks - 1; block > last_kernel_block; --block) {
    disk_.Seek(base::File::FROM_BEGIN, kBlockSize * block);
    uint64_t block_magic = 0;
    disk_.ReadAtCurrentPos(reinterpret_cast<char*>(&block_magic),
                           sizeof(block_magic));
    if (block_magic == kLogStoreMagic) {
      return kBlockSize * block;
    }
  }
  // Return `nullopt` if no magic is found.
  return std::nullopt;
}

}  // namespace minios

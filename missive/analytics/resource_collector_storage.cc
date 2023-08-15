// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/analytics/resource_collector_storage.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>

#include "missive/analytics/metrics.h"

namespace reporting::analytics {

ResourceCollectorStorage::ResourceCollectorStorage(
    base::TimeDelta interval, const base::FilePath& storage_directory)
    : ResourceCollector(interval), storage_directory_(storage_directory) {}

ResourceCollectorStorage::~ResourceCollectorStorage() {
  StopTimer();
}

int ResourceCollectorStorage::ConvertBytesToMibs(int bytes) {
  // Round the result to the nearest MiB.
  // As a special circumstance, if the rounded size in MiB is zero, then we give
  // it 1.
  return std::max((bytes + 1024 * 1024 / 2) / (1024 * 1024), 1);
}

void ResourceCollectorStorage::Collect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto storage_size = base::ComputeDirectorySize(storage_directory_);
  // Upload storage size as total usage UMA.
  if (!SendDirectorySizeToUma(kUmaName, storage_size)) {
    LOG(ERROR) << "Failed to send directory size to UMA.";
  }
  // If there was no successful uploads for more than 1 day, upload the same
  // storage size as non-uploading usage as well.
  if (upload_progress_timestamp_.load() + base::Days(1) < base::Time::Now()) {
    if (!SendDirectorySizeToUma(kNonUploadingUmaName, storage_size)) {
      LOG(ERROR) << "Failed to send directory size to UMA.";
    }
    upload_progress_timestamp_ = base::Time::Now();  // reset for the next time.
  }
}

bool ResourceCollectorStorage::SendDirectorySizeToUma(std::string_view uma_name,
                                                      int directory_size) {
  return Metrics::SendToUMA(
      /*name=*/std::string(uma_name),
      /*sample=*/ConvertBytesToMibs(directory_size),
      /*min=*/kMin,
      /*max=*/kMax,
      /*nbuckets=*/kUmaNumberOfBuckets);
}

void ResourceCollectorStorage::RecordUploadProgress() {
  upload_progress_timestamp_ = base::Time::Now();
}
}  // namespace reporting::analytics

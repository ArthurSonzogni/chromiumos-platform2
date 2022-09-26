// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_configuration.h"

namespace reporting {

namespace {

// Parameters of individual queues.
// TODO(b/159352842): Deliver space and upload parameters from outside.

constexpr char kSecurityQueueSubdir[] = "Security";
constexpr char kSecurityQueuePrefix[] = "P_Security";

constexpr char kImmediateQueueSubdir[] = "Immediate";
constexpr char kImmediateQueuePrefix[] = "P_Immediate";

constexpr char kFastBatchQueueSubdir[] = "FastBatch";
constexpr char kFastBatchQueuePrefix[] = "P_FastBatch";
constexpr base::TimeDelta kFastBatchUploadPeriod = base::Seconds(1);

constexpr char kSlowBatchQueueSubdir[] = "SlowBatch";
constexpr char kSlowBatchQueuePrefix[] = "P_SlowBatch";
constexpr base::TimeDelta kSlowBatchUploadPeriod = base::Seconds(20);

constexpr char kBackgroundQueueSubdir[] = "Background";
constexpr char kBackgroundQueuePrefix[] = "P_Background";
constexpr base::TimeDelta kBackgroundQueueUploadPeriod = base::Minutes(1);

constexpr char kManualQueueSubdir[] = "Manual";
constexpr char kManualQueuePrefix[] = "P_Manual";
constexpr base::TimeDelta kManualUploadPeriod = base::TimeDelta::Max();

// Failed upload retry delay: if an upload fails and there are no more incoming
// events, collected events will not get uploaded for an indefinite time (see
// b/192666219).
constexpr base::TimeDelta kFailedUploadRetryDelay = base::Seconds(1);

}  // namespace

StorageOptions::StorageOptions()
    : memory_resource_(base::MakeRefCounted<MemoryResourceImpl>(
          4u * 1024uLL * 1024uLL)),  // 4 MiB by default
      disk_space_resource_(base::MakeRefCounted<DiskResourceImpl>(
          64u * 1024uLL * 1024uLL))  // 64 MiB by default.
{}
StorageOptions::StorageOptions(const StorageOptions& options) = default;
StorageOptions::~StorageOptions() = default;

// Returns vector of <priority, queue_options> for all expected queues in
// Storage. Queues are all located under the given root directory.
StorageOptions::QueuesOptionsList StorageOptions::ProduceQueuesOptions() const {
  return {
      std::make_pair(MANUAL_BATCH,
                     QueueOptions(*this)
                         .set_subdirectory(kManualQueueSubdir)
                         .set_file_prefix(kManualQueuePrefix)
                         .set_upload_period(kManualUploadPeriod)
                         .set_upload_retry_delay(kFailedUploadRetryDelay)),
      std::make_pair(BACKGROUND_BATCH,
                     QueueOptions(*this)
                         .set_subdirectory(kBackgroundQueueSubdir)
                         .set_file_prefix(kBackgroundQueuePrefix)
                         .set_upload_period(kBackgroundQueueUploadPeriod)),
      std::make_pair(SLOW_BATCH,
                     QueueOptions(*this)
                         .set_subdirectory(kSlowBatchQueueSubdir)
                         .set_file_prefix(kSlowBatchQueuePrefix)
                         .set_upload_period(kSlowBatchUploadPeriod)),
      std::make_pair(FAST_BATCH,
                     QueueOptions(*this)
                         .set_subdirectory(kFastBatchQueueSubdir)
                         .set_file_prefix(kFastBatchQueuePrefix)
                         .set_upload_period(kFastBatchUploadPeriod)),
      std::make_pair(IMMEDIATE,
                     QueueOptions(*this)
                         .set_subdirectory(kImmediateQueueSubdir)
                         .set_file_prefix(kImmediateQueuePrefix)
                         .set_upload_retry_delay(kFailedUploadRetryDelay)),
      std::make_pair(SECURITY,
                     QueueOptions(*this)
                         .set_subdirectory(kSecurityQueueSubdir)
                         .set_file_prefix(kSecurityQueuePrefix)
                         .set_upload_retry_delay(kFailedUploadRetryDelay)),
  };
}

QueueOptions::QueueOptions(const StorageOptions& storage_options)
    : storage_options_(storage_options) {}
QueueOptions::QueueOptions(const QueueOptions& options) = default;
}  // namespace reporting

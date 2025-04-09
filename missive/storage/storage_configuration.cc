// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_configuration.h"

#include <base/containers/span.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>

#include "missive/proto/record_constants.pb.h"
#include "missive/resources/resource_manager.h"

// Temporary replacement for `Priority_Name` that does
// not work in certain CQ.
// TODO(b/294756107): Remove this function once fixed.
#include "missive/proto/priority_name.h"

namespace reporting {

StorageOptions::MultiGenerational::MultiGenerational() {
  for (const auto& priority : StorageOptions::GetPrioritiesOrder()) {
    is_multi_generational_[priority].store(false);
  }
}

bool StorageOptions::MultiGenerational::get(Priority priority) const {
  CHECK_LT(priority, Priority_ARRAYSIZE);
  return is_multi_generational_[priority].load();
}

void StorageOptions::MultiGenerational::set(Priority priority, bool state) {
  CHECK_LT(priority, Priority_ARRAYSIZE);
  const bool was_multigenerational =
      is_multi_generational_[priority].exchange(state);
  LOG_IF(WARNING, was_multigenerational != state)
      << "Priority " << Priority_Name_Substitute(priority) << " switched to "
      << (state ? "multi" : "single") << "-generational state";
}

StorageOptions::StorageOptions(
    base::RepeatingCallback<void(Priority, QueueOptions&)>
        modify_queue_options_for_tests)
    : key_check_period_(kDefaultKeyCheckPeriod),
      lazy_key_check_period_(kLazyDefaultKeyCheckPeriod),
      is_multi_generational_(base::MakeRefCounted<MultiGenerational>()),
      memory_resource_(base::MakeRefCounted<ResourceManager>(
          4u * 1024uLL * 1024uLL)),  // 4 MiB by default
      disk_space_resource_(base::MakeRefCounted<ResourceManager>(
          64u * 1024uLL * 1024uLL)),  // 64 MiB by default.
      modify_queue_options_for_tests_(modify_queue_options_for_tests) {}

StorageOptions::StorageOptions(const StorageOptions& other) = default;
StorageOptions::~StorageOptions() = default;

QueueOptions StorageOptions::PopulateQueueOptions(Priority priority) const {
  switch (priority) {
    case MANUAL_BATCH_LACROS:
      return QueueOptions(*this)
          .set_subdirectory(kManualLacrosQueueSubdir)
          .set_file_prefix(kManualLacrosQueuePrefix)
          .set_upload_period(kManualUploadPeriod)
          .set_upload_retry_delay(kFailedUploadRetryDelay);
    case MANUAL_BATCH:
      return QueueOptions(*this)
          .set_subdirectory(kManualQueueSubdir)
          .set_file_prefix(kManualQueuePrefix)
          .set_upload_period(kManualUploadPeriod)
          .set_upload_retry_delay(kFailedUploadRetryDelay);
    case BACKGROUND_BATCH:
      return QueueOptions(*this)
          .set_subdirectory(kBackgroundQueueSubdir)
          .set_file_prefix(kBackgroundQueuePrefix)
          .set_upload_period(kBackgroundQueueUploadPeriod);
    case SLOW_BATCH:
      return QueueOptions(*this)
          .set_subdirectory(kSlowBatchQueueSubdir)
          .set_file_prefix(kSlowBatchQueuePrefix)
          .set_upload_period(kSlowBatchUploadPeriod);
    case FAST_BATCH:
      return QueueOptions(*this)
          .set_subdirectory(kFastBatchQueueSubdir)
          .set_file_prefix(kFastBatchQueuePrefix)
          .set_upload_period(kFastBatchUploadPeriod);
    case IMMEDIATE:
      return QueueOptions(*this)
          .set_subdirectory(kImmediateQueueSubdir)
          .set_file_prefix(kImmediateQueuePrefix)
          .set_upload_retry_delay(kFailedUploadRetryDelay);
    case SECURITY:
      return QueueOptions(*this)
          .set_subdirectory(kSecurityQueueSubdir)
          .set_file_prefix(kSecurityQueuePrefix)
          .set_upload_retry_delay(kFailedUploadRetryDelay)
          .set_can_shed_records(false);
    case UNDEFINED_PRIORITY:
      NOTREACHED() << "No QueueOptions for priority UNDEFINED_PRIORITY.";
  }
}

QueueOptions StorageOptions::ProduceQueueOptions(Priority priority) const {
  QueueOptions queue_options(PopulateQueueOptions(priority));
  modify_queue_options_for_tests_.Run(priority, queue_options);
  return queue_options;
}

StorageOptions::QueuesOptionsList StorageOptions::ProduceQueuesOptionsList()
    const {
  QueuesOptionsList queue_options_list;
  // Create queue option for each priority and add to the list.
  for (const auto priority : GetPrioritiesOrder()) {
    queue_options_list.emplace_back(priority, ProduceQueueOptions(priority));
  }
  return queue_options_list;
}

// static
base::span<const Priority> StorageOptions::GetPrioritiesOrder() {
  // Order of priorities
  static constexpr std::array<Priority, 7> kPriorityOrder = {
      MANUAL_BATCH_LACROS, MANUAL_BATCH, BACKGROUND_BATCH, SLOW_BATCH,
      FAST_BATCH,          IMMEDIATE,    SECURITY};
  return kPriorityOrder;
}

QueueOptions::QueueOptions(const StorageOptions& storage_options)
    : storage_options_(storage_options) {}
QueueOptions::QueueOptions(const QueueOptions& options) = default;

}  // namespace reporting

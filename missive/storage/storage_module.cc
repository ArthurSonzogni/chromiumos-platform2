// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_module.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequence_checker.h>
#include <base/strings/string_piece_forward.h>
#include <base/strings/string_split.h>
#include <base/task/bind_post_task.h>
#include <base/task/thread_pool.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/new_storage.h"
#include "missive/storage/storage_base.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

const Status kStorageUnavailableStatus =
    Status(error::UNAVAILABLE, "Storage unavailable");

StorageModule::StorageModule(
    const StorageOptions& options,
    UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
    scoped_refptr<QueuesContainer> queues_container,
    scoped_refptr<EncryptionModuleInterface> encryption_module,
    scoped_refptr<CompressionModule> compression_module,
    scoped_refptr<SignatureVerificationDevFlag> signature_verification_dev_flag)
    : sequenced_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()})),
      options_(options),
      async_start_upload_cb_(async_start_upload_cb),
      queues_container_(queues_container),
      encryption_module_(encryption_module),
      compression_module_(compression_module),
      signature_verification_dev_flag_(signature_verification_dev_flag) {
  // Constructor may be called on any thread.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

StorageModule::~StorageModule() = default;

void StorageModule::AddRecord(Priority priority,
                              Record record,
                              EnqueueCallback callback) {
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     [](scoped_refptr<StorageModule> self, Priority priority,
                        Record record, EnqueueCallback callback) {
                       DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
                       if (!(self->storage_)) {
                         std::move(callback).Run(kStorageUnavailableStatus);
                         return;
                       }
                       self->storage_->Write(priority, std::move(record),
                                             std::move(callback));
                     },
                     base::WrapRefCounted(this), priority, std::move(record),
                     std::move(callback)));
}

void StorageModule::Flush(Priority priority, FlushCallback callback) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<StorageModule> self, Priority priority,
             FlushCallback callback) {
            DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
            if (!(self->storage_)) {
              std::move(callback).Run(kStorageUnavailableStatus);
              return;
            }
            self->storage_->Flush(priority, std::move(callback));
          },
          base::WrapRefCounted(this), priority, std::move(callback)));
}

void StorageModule::ReportSuccess(SequenceInformation sequence_information,
                                  bool force) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<StorageModule> self,
             SequenceInformation sequence_information, bool force) {
            DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
            if (!(self->storage_)) {
              LOG(ERROR) << kStorageUnavailableStatus.error_message();
              return;
            }
            self->storage_->Confirm(
                std::move(sequence_information), force,
                base::BindOnce([](Status status) {
                  LOG_IF(ERROR, !status.ok())
                      << "Unable to confirm record deletion: " << status;
                }));
          },
          base::WrapRefCounted(this), std::move(sequence_information), force));
}

void StorageModule::UpdateEncryptionKey(
    SignedEncryptionInfo signed_encryption_key) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<StorageModule> self,
             SignedEncryptionInfo signed_encryption_key) {
            DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
            if (!(self->storage_)) {
              LOG(ERROR) << kStorageUnavailableStatus.error_message();
              return;
            }
            self->storage_->UpdateEncryptionKey(
                std::move(signed_encryption_key));
          },
          base::WrapRefCounted(this), std::move(signed_encryption_key)));
}

void StorageModule::SetLegacyEnabledPriorities(
    base::StringPiece legacy_storage_enabled) {
  const std::vector<base::StringPiece> splits =
      base::SplitStringPieceUsingSubstr(legacy_storage_enabled, ",",
                                        base::TRIM_WHITESPACE,
                                        base::SPLIT_WANT_NONEMPTY);
  // Initialize all flags as 'false' (multi-generational, non-legacy).
  std::array<bool, Priority_ARRAYSIZE> legacy_enabled_for_priority;
  for (auto& value : legacy_enabled_for_priority) {
    value = false;
  }
  // Flip specified priorities' flags as 'true' (single-generation, legacy).
  for (const auto& split : splits) {
    Priority priority;
    if (!Priority_Parse(std::string(split), &priority)) {
      LOG(ERROR) << "Invalid legacy-enabled priority specified: `" << split
                 << "`";
      continue;
    }
    DCHECK_LT(priority, Priority_ARRAYSIZE);
    legacy_enabled_for_priority[priority] = true;
  }
  // Atomically deliver all priorities' flags to `options_` (shared with
  // `storage_`). For flags that do not change `set_multi_generational` is
  // effectively a no-op.
  for (const auto& priority : StorageOptions::GetPrioritiesOrder()) {
    options_.set_multi_generational(priority,
                                    !legacy_enabled_for_priority[priority]);
  }
}

// static
void StorageModule::Create(
    const StorageOptions& options,
    base::StringPiece legacy_storage_enabled,
    UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
    scoped_refptr<QueuesContainer> queues_container,
    scoped_refptr<EncryptionModuleInterface> encryption_module,
    scoped_refptr<CompressionModule> compression_module,
    scoped_refptr<SignatureVerificationDevFlag> signature_verification_dev_flag,
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)> callback) {
  // Call constructor.
  scoped_refptr<StorageModule> instance =
      // Cannot use `base::MakeRefCounted`, since constructor is protected.
      base::WrapRefCounted(new StorageModule(
          options, async_start_upload_cb, queues_container, encryption_module,
          compression_module, signature_verification_dev_flag));

  // Enable/disable multi-generation action for all priorities.
  instance->SetLegacyEnabledPriorities(legacy_storage_enabled);

  // Initialize `instance`.
  InitStorageAsync(instance, std::move(callback)).Run();
}

// static
base::OnceClosure StorageModule::InitStorageAsync(
    scoped_refptr<StorageModule> instance,
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)> callback) {
  return base::BindPostTask(
      instance->sequenced_task_runner_,
      base::BindOnce(
          [](scoped_refptr<StorageModule> self,
             base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
                 callback) {
            // Partially bound callback which sets `storage_` or returns an
            // error status via `callback`. Run on `sequenced_task_runner_`.
            auto set_storage_cb =
                base::BindPostTask(self->sequenced_task_runner_,
                                   base::BindOnce(&StorageModule::SetStorage,
                                                  self, std::move(callback)));

            // Instantiate Storage.
            NewStorage::Create(self->options_, self->async_start_upload_cb_,
                               self->queues_container_,
                               self->encryption_module_,
                               self->compression_module_,
                               self->signature_verification_dev_flag_,
                               std::move(set_storage_cb));
          },
          instance, std::move(callback)));
}

void StorageModule::SetStorage(
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)> callback,
    StatusOr<scoped_refptr<StorageInterface>> storage) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!storage.ok()) {
    std::move(callback).Run(storage.status());
    return;
  }
  storage_ = storage.ValueOrDie();
  std::move(callback).Run(base::WrapRefCounted(this));
}

void StorageModule::InjectStorageUnavailableErrorForTesting() {
  storage_.reset();
}

}  // namespace reporting

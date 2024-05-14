// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/key_delivery.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/thread_pool.h>
#include <base/thread_annotations.h>
#include <base/timer/timer.h>

#include "missive/analytics/metrics.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"

namespace reporting {

// Factory method, returns smart pointer with deletion on sequence.
std::unique_ptr<KeyDelivery, base::OnTaskRunnerDeleter> KeyDelivery::Create(
    scoped_refptr<EncryptionModuleInterface> encryption_module,
    UploaderInterface::AsyncStartUploaderCb async_start_upload_cb) {
  auto sequence_task_runner = base::ThreadPool::CreateSequencedTaskRunner(
      {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return std::unique_ptr<KeyDelivery, base::OnTaskRunnerDeleter>(
      new KeyDelivery(encryption_module, async_start_upload_cb,
                      sequence_task_runner),
      base::OnTaskRunnerDeleter(sequence_task_runner));
}

KeyDelivery::~KeyDelivery() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  upload_timer_.Stop();
  PostResponses(
      Status(error::UNAVAILABLE, "Key not delivered - Storage shuts down"));
}

void KeyDelivery::Request(bool is_mandatory, RequestCallback callback) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&KeyDelivery::EnqueueRequestAndPossiblyStart,
                     base::Unretained(this), is_mandatory,
                     base::BindOnce([](Status status) {
                       // Log the request status in UMA
                       const auto res = analytics::Metrics::SendEnumToUMA(
                           /*name=*/KeyDelivery::kResultUma, status.code(),
                           error::Code::MAX_VALUE);
                       LOG_IF(ERROR, !res) << "SendEnumToUMA failure, "
                                           << KeyDelivery::kResultUma << " "
                                           << static_cast<int>(status.code());
                       return status;
                     }).Then(std::move(callback))));
}

void KeyDelivery::OnCompletion(Status status) {
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&KeyDelivery::PostResponses,
                                base::Unretained(this), status));
}

void KeyDelivery::StartPeriodicKeyUpdate(const base::TimeDelta period) {
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     [](KeyDelivery* self, base::TimeDelta period) {
                       if (self->upload_timer_.IsRunning()) {
                         // We've already started the periodic key update.
                         return;
                       }
                       // `base::Unretained` is ok here because `upload_timer_`
                       // is destructed in the class destructor, and so is the
                       // callback.
                       self->upload_timer_.Start(
                           FROM_HERE, period,
                           base::BindRepeating(&KeyDelivery::RequestKeyIfNeeded,
                                               base::Unretained(self)));
                     },
                     base::Unretained(this), period));
}

KeyDelivery::KeyDelivery(
    scoped_refptr<EncryptionModuleInterface> encryption_module,
    UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner)
    : sequenced_task_runner_(sequenced_task_runner),
      async_start_upload_cb_(async_start_upload_cb),
      encryption_module_(encryption_module) {
  CHECK(encryption_module_) << "Encryption module pointer not set";
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

void KeyDelivery::RequestKeyIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (encryption_module_->has_encryption_key() &&
      !encryption_module_->need_encryption_key()) {
    return;
  }
  // Request the key
  Request(/*is_mandatory=*/false, base::DoNothing());
}

void KeyDelivery::EnqueueRequestAndPossiblyStart(bool is_mandatory,
                                                 RequestCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(callback);
  if (is_mandatory || callbacks_.empty()) {
    callbacks_.push_back(std::move(callback));
  }

  // Initiate upload with need_encryption_key flag and no records.
  UploaderInterface::UploaderInterfaceResultCb start_uploader_cb =
      base::BindOnce(&KeyDelivery::EncryptionKeyReceiverReady,
                     base::Unretained(this));
  async_start_upload_cb_.Run(UploaderInterface::UploadReason::KEY_DELIVERY,
                             /*inform_cb=*/base::DoNothing(),  // No records.
                             std::move(start_uploader_cb));
}

void KeyDelivery::PostResponses(Status status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& callback : callbacks_) {
    std::move(callback).Run(status);
  }
  callbacks_.clear();
}

void KeyDelivery::EncryptionKeyReceiverReady(
    StatusOr<std::unique_ptr<UploaderInterface>> uploader_result) {
  if (!uploader_result.has_value()) {
    OnCompletion(uploader_result.error());
    return;
  }
  uploader_result.value()->Completed(Status::StatusOK());
}
}  // namespace reporting

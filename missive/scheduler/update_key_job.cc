// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/update_key_job.h"

#include <memory>
#include <utility>

#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>

#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module.h"
#include "missive/util/status.h"

namespace reporting {

UpdateEncryptionKeyJob::UpdateEncryptionKeyResponseDelegate::
    UpdateEncryptionKeyResponseDelegate(
        std::unique_ptr<
            brillo::dbus_utils::DBusMethodResponse<UpdateEncryptionKeyResponse>>
            response)
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      response_(std::move(response)) {
  CHECK(task_runner_);
  CHECK(response_);
}

Status UpdateEncryptionKeyJob::UpdateEncryptionKeyResponseDelegate::Complete() {
  return SendResponse(Status::StatusOK());
}

Status UpdateEncryptionKeyJob::UpdateEncryptionKeyResponseDelegate::Cancel(
    Status status) {
  return SendResponse(status);
}

Status
UpdateEncryptionKeyJob::UpdateEncryptionKeyResponseDelegate::SendResponse(
    Status status) {
  UpdateEncryptionKeyResponse response_body;
  status.SaveTo(response_body.mutable_status());

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&brillo::dbus_utils::DBusMethodResponse<
                         UpdateEncryptionKeyResponse>::Return,
                     std::move(response_), std::move(response_body)));
  return Status::StatusOK();
}

// static
Scheduler::Job::SmartPtr<UpdateEncryptionKeyJob> UpdateEncryptionKeyJob::Create(
    scoped_refptr<StorageModule> storage_module,
    UpdateEncryptionKeyRequest request,
    std::unique_ptr<UpdateEncryptionKeyResponseDelegate> delegate) {
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return std::unique_ptr<UpdateEncryptionKeyJob, base::OnTaskRunnerDeleter>(
      new UpdateEncryptionKeyJob(storage_module, sequenced_task_runner,
                                 std::move(request), std::move(delegate)),
      base::OnTaskRunnerDeleter(sequenced_task_runner));
}

UpdateEncryptionKeyJob::UpdateEncryptionKeyJob(
    scoped_refptr<StorageModule> storage_module,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
    UpdateEncryptionKeyRequest request,
    std::unique_ptr<UpdateEncryptionKeyResponseDelegate> delegate)
    : Job(std::move(delegate), sequenced_task_runner),
      storage_module_(storage_module),
      request_(std::move(request)) {}

void UpdateEncryptionKeyJob::StartImpl() {
  if (!request_.has_signed_encryption_info()) {
    sequenced_task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&UpdateEncryptionKeyJob::Finish,
                       weak_ptr_factory_.GetWeakPtr(),
                       Status(error::INVALID_ARGUMENT,
                              "Request had no SignedEncryptionInfo")));
    return;
  }

  storage_module_->UpdateEncryptionKey(request_.signed_encryption_info());
  sequenced_task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&UpdateEncryptionKeyJob::Finish,
                     weak_ptr_factory_.GetWeakPtr(), Status::StatusOK()));
}

}  // namespace reporting

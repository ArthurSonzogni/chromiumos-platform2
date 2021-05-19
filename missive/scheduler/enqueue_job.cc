// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/enqueue_job.h"

#include <memory>
#include <string>
#include <utility>

#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"

namespace reporting {

EnqueueJob::EnqueueResponseDelegate::EnqueueResponseDelegate(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>> response)
    : task_runner_(base::SequencedTaskRunnerHandle::Get()),
      response_(std::move(response)) {
  DCHECK(task_runner_);
  DCHECK(response_);
}

Status EnqueueJob::EnqueueResponseDelegate::Complete() {
  return SendResponse(Status::StatusOK());
}

Status EnqueueJob::EnqueueResponseDelegate::Cancel(Status status) {
  return SendResponse(status);
}

Status EnqueueJob::EnqueueResponseDelegate::SendResponse(Status status) {
  EnqueueRecordResponse response_body;
  status.SaveTo(response_body.mutable_status());
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&brillo::dbus_utils::DBusMethodResponse<
                         EnqueueRecordResponse>::Return,
                     std::move(response_), std::move(response_body)));
  return Status::StatusOK();
}

EnqueueJob::EnqueueJob(scoped_refptr<StorageModuleInterface> storage_module,
                       EnqueueRecordRequest request,
                       std::unique_ptr<EnqueueResponseDelegate> delegate)
    : Job(std::move(delegate)),
      storage_module_(storage_module),
      request_(std::move(request)) {}

void EnqueueJob::StartImpl() {
  storage_module_->AddRecord(
      request_.priority(), std::move(request_.record()),
      base::BindOnce(&EnqueueJob::Finish, base::Unretained(this)));
}

}  // namespace reporting

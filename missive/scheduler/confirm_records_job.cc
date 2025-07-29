// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/confirm_records_job.h"

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
#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"

namespace reporting {

ConfirmRecordsJob::ConfirmRecordsResponseDelegate::
    ConfirmRecordsResponseDelegate(
        scoped_refptr<HealthModule> health_module,
        std::unique_ptr<
            brillo::dbus_utils::DBusMethodResponse<ConfirmRecordUploadResponse>>
            response)
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      health_module_(health_module),
      response_(std::move(response)) {
  CHECK(task_runner_);
  CHECK(response_);
}

Status ConfirmRecordsJob::ConfirmRecordsResponseDelegate::Complete() {
  return SendResponse(Status::StatusOK());
}

Status ConfirmRecordsJob::ConfirmRecordsResponseDelegate::Cancel(
    Status status) {
  return SendResponse(status);
}

Status ConfirmRecordsJob::ConfirmRecordsResponseDelegate::SendResponse(
    Status status) {
  ConfirmRecordUploadResponse response_body;
  status.SaveTo(response_body.mutable_status());

  auto response_cb = base::BindPostTask(
      task_runner_, base::BindOnce(&brillo::dbus_utils::DBusMethodResponse<
                                       ConfirmRecordUploadResponse>::Return,
                                   std::move(response_)));
  if (!health_module_->is_debugging()) {
    std::move(response_cb).Run(std::move(response_body));
    return Status::StatusOK();
  }

  health_module_->GetHealthData(
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          [](base::OnceCallback<void(const ConfirmRecordUploadResponse&)>
                 response_cb,
             ConfirmRecordUploadResponse response_body,
             ERPHealthData health_data) {
            *response_body.mutable_health_data() = std::move(health_data);
            std::move(response_cb).Run(std::move(response_body));
          },
          std::move(response_cb), std::move(response_body))));
  return Status::StatusOK();
}

// static
Scheduler::Job::SmartPtr<ConfirmRecordsJob> ConfirmRecordsJob::Create(
    scoped_refptr<StorageModule> storage_module,
    scoped_refptr<HealthModule> health_module,
    ConfirmRecordUploadRequest request,
    std::unique_ptr<ConfirmRecordsResponseDelegate> delegate) {
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return Scheduler::Job::SmartPtr<ConfirmRecordsJob>(
      new ConfirmRecordsJob(storage_module, health_module,
                            sequenced_task_runner, std::move(request),
                            std::move(delegate)),
      base::OnTaskRunnerDeleter(sequenced_task_runner));
}

ConfirmRecordsJob::ConfirmRecordsJob(
    scoped_refptr<StorageModule> storage_module,
    scoped_refptr<HealthModule> health_module,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
    ConfirmRecordUploadRequest request,
    std::unique_ptr<ConfirmRecordsResponseDelegate> delegate)
    : Job(std::move(delegate), sequenced_task_runner),
      storage_module_(storage_module),
      health_module_(health_module),
      request_(std::move(request)) {}

void ConfirmRecordsJob::StartImpl() {
  if (!request_.has_sequence_information()) {
    sequenced_task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&ConfirmRecordsJob::Finish,
                       weak_ptr_factory_.GetWeakPtr(),
                       Status(error::INVALID_ARGUMENT,
                              "Request had no SequenceInformation")));
    return;
  }
  if (request_.has_health_data_logging_enabled()) {
    health_module_->set_debugging(request_.health_data_logging_enabled());
  }
  storage_module_->ReportSuccess(
      request_.sequence_information(), request_.force_confirm(),
      base::BindPostTask(sequenced_task_runner(),
                         base::BindOnce(&ConfirmRecordsJob::Finish,
                                        weak_ptr_factory_.GetWeakPtr())));
}

}  // namespace reporting

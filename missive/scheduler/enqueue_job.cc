// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/enqueue_job.h"

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

EnqueueJob::EnqueueResponseDelegate::EnqueueResponseDelegate(
    scoped_refptr<HealthModule> health_module,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>> response)
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      health_module_(health_module),
      response_(std::move(response)) {
  CHECK(task_runner_);
  CHECK(response_);
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

  auto response_cb = base::BindPostTask(
      task_runner_, base::BindOnce(&brillo::dbus_utils::DBusMethodResponse<
                                       EnqueueRecordResponse>::Return,
                                   std::move(response_)));
  if (!health_module_->is_debugging()) {
    std::move(response_cb).Run(std::move(response_body));
    return Status::StatusOK();
  }

  health_module_->GetHealthData(
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          [](base::OnceCallback<void(const EnqueueRecordResponse&)> response_cb,
             EnqueueRecordResponse response_body, ERPHealthData health_data) {
            *response_body.mutable_health_data() = std::move(health_data);
            std::move(response_cb).Run(std::move(response_body));
          },
          std::move(response_cb), std::move(response_body))));
  return Status::StatusOK();
}

// static
Scheduler::Job::SmartPtr<EnqueueJob> EnqueueJob::Create(
    scoped_refptr<StorageModuleInterface> storage_module,
    scoped_refptr<HealthModule> health_module,
    EnqueueRecordRequest request,
    std::unique_ptr<EnqueueResponseDelegate> delegate) {
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return std::unique_ptr<EnqueueJob, base::OnTaskRunnerDeleter>(
      new EnqueueJob(storage_module, health_module, sequenced_task_runner,
                     std::move(request), std::move(delegate)),
      base::OnTaskRunnerDeleter(sequenced_task_runner));
}

EnqueueJob::EnqueueJob(
    scoped_refptr<StorageModuleInterface> storage_module,
    scoped_refptr<HealthModule> health_module,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
    EnqueueRecordRequest request,
    std::unique_ptr<EnqueueResponseDelegate> delegate)
    : Job(std::move(delegate), sequenced_task_runner),
      storage_module_(storage_module),
      health_module_(health_module),
      request_(std::move(request)) {}

void EnqueueJob::StartImpl() {
  if (request_.has_health_data_logging_enabled()) {
    health_module_->set_debugging(request_.health_data_logging_enabled());
  }
  storage_module_->AddRecord(
      request_.priority(), std::move(request_.record()),
      base::BindPostTask(
          sequenced_task_runner(),
          base::BindOnce(&EnqueueJob::Finish, weak_ptr_factory_.GetWeakPtr())));
}

}  // namespace reporting

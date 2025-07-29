// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/flush_job.h"

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

FlushJob::FlushResponseDelegate::FlushResponseDelegate(
    scoped_refptr<HealthModule> health_module,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<FlushPriorityResponse>> response)
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      health_module_(health_module),
      response_(std::move(response)) {
  CHECK(task_runner_);
  CHECK(response_);
}

Status FlushJob::FlushResponseDelegate::Complete() {
  return SendResponse(Status::StatusOK());
}

Status FlushJob::FlushResponseDelegate::Cancel(Status status) {
  return SendResponse(status);
}

Status FlushJob::FlushResponseDelegate::SendResponse(Status status) {
  FlushPriorityResponse response_body;
  status.SaveTo(response_body.mutable_status());

  auto response_cb = base::BindPostTask(
      task_runner_, base::BindOnce(&brillo::dbus_utils::DBusMethodResponse<
                                       FlushPriorityResponse>::Return,
                                   std::move(response_)));
  if (!health_module_->is_debugging()) {
    std::move(response_cb).Run(std::move(response_body));
    return Status::StatusOK();
  }

  health_module_->GetHealthData(
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          [](base::OnceCallback<void(const FlushPriorityResponse&)> response_cb,
             FlushPriorityResponse response_body, ERPHealthData health_data) {
            *response_body.mutable_health_data() = std::move(health_data);
            std::move(response_cb).Run(std::move(response_body));
          },
          std::move(response_cb), std::move(response_body))));
  return Status::StatusOK();
}

// static
Scheduler::Job::SmartPtr<FlushJob> FlushJob::Create(
    scoped_refptr<StorageModuleInterface> storage_module,
    scoped_refptr<HealthModule> health_module,
    FlushPriorityRequest request,
    std::unique_ptr<FlushResponseDelegate> delegate) {
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return Scheduler::Job::SmartPtr<FlushJob>(
      new FlushJob(storage_module, health_module, sequenced_task_runner,
                   std::move(request), std::move(delegate)),
      base::OnTaskRunnerDeleter(sequenced_task_runner));
}

FlushJob::FlushJob(
    scoped_refptr<StorageModuleInterface> storage_module,
    scoped_refptr<HealthModule> health_module,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
    FlushPriorityRequest request,
    std::unique_ptr<FlushResponseDelegate> delegate)
    : Job(std::move(delegate), sequenced_task_runner),
      storage_module_(storage_module),
      health_module_(health_module),
      request_(std::move(request)) {}

void FlushJob::StartImpl() {
  if (request_.has_health_data_logging_enabled()) {
    health_module_->set_debugging(request_.health_data_logging_enabled());
  }
  storage_module_->Flush(
      request_.priority(),
      base::BindPostTask(
          sequenced_task_runner(),
          base::BindOnce(&FlushJob::Finish, weak_ptr_factory_.GetWeakPtr())));
}

}  // namespace reporting

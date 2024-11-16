// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/update_config_job.h"

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
#include "missive/util/server_configuration_controller.h"
#include "missive/util/status.h"

namespace reporting {

UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate::
    UpdateConfigInMissiveResponseDelegate(
        std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
            UpdateConfigInMissiveResponse>> response)
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      response_(std::move(response)) {
  CHECK(task_runner_);
  CHECK(response_);
}

Status
UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate::Complete() {
  return SendResponse(Status::StatusOK());
}

Status UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate::Cancel(
    Status status) {
  return SendResponse(status);
}

Status
UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate::SendResponse(
    Status status) {
  UpdateConfigInMissiveResponse response_body;
  status.SaveTo(response_body.mutable_status());

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&brillo::dbus_utils::DBusMethodResponse<
                         UpdateConfigInMissiveResponse>::Return,
                     std::move(response_), std::move(response_body)));
  return Status::StatusOK();
}

// static
Scheduler::Job::SmartPtr<UpdateConfigInMissiveJob>
UpdateConfigInMissiveJob::Create(
    scoped_refptr<HealthModule> health_module,
    scoped_refptr<ServerConfigurationController>
        server_configuration_controller,
    UpdateConfigInMissiveRequest request,
    std::unique_ptr<UpdateConfigInMissiveResponseDelegate> delegate) {
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return std::unique_ptr<UpdateConfigInMissiveJob, base::OnTaskRunnerDeleter>(
      new UpdateConfigInMissiveJob(
          health_module, server_configuration_controller, sequenced_task_runner,
          std::move(request), std::move(delegate)),
      base::OnTaskRunnerDeleter(sequenced_task_runner));
}

UpdateConfigInMissiveJob::UpdateConfigInMissiveJob(
    scoped_refptr<HealthModule> health_module,
    scoped_refptr<ServerConfigurationController>
        server_configuration_controller,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
    UpdateConfigInMissiveRequest request,
    std::unique_ptr<UpdateConfigInMissiveResponseDelegate> delegate)
    : Job(std::move(delegate), sequenced_task_runner),
      health_module_(health_module),
      server_configuration_controller_(server_configuration_controller),
      request_(std::move(request)) {}

void UpdateConfigInMissiveJob::StartImpl() {
  if (!request_.has_list_of_blocked_destinations()) {
    sequenced_task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&UpdateConfigInMissiveJob::Finish,
                       weak_ptr_factory_.GetWeakPtr(),
                       Status(error::INVALID_ARGUMENT,
                              "Request had no ListOfBlockedDestinations")));
    return;
  }
  // Provide health module recorder, if debugging is enabled.
  auto recorder = health_module_->NewRecorder();
  server_configuration_controller_->UpdateConfiguration(
      request_.list_of_blocked_destinations(), std::move(recorder));
  sequenced_task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&UpdateConfigInMissiveJob::Finish,
                     weak_ptr_factory_.GetWeakPtr(), Status::StatusOK()));
}

}  // namespace reporting

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/report_queue_factory.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/thread_pool.h>

#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_provider.h"

#define LOG_WITH_STATUS(LEVEL, MESSAGE, STATUS) \
  VLOG(LEVEL) << MESSAGE << " status=" << STATUS.error();

namespace reporting {

namespace {

void TrySetReportQueue(
    ReportQueueFactory::SuccessCallback success_cb,
    StatusOr<std::unique_ptr<ReportQueue>> report_queue_result) {
  if (!report_queue_result.has_value()) {
    LOG_WITH_STATUS(1, "ReportQueue could not be created.",
                    report_queue_result);
    return;
  }
  std::move(success_cb).Run(std::move(report_queue_result.value()));
}
}  // namespace

// static
void ReportQueueFactory::Create(EventType event_type,
                                Destination destination,
                                SuccessCallback success_cb,
                                int64_t reserved_space) {
  CHECK(base::SequencedTaskRunner::HasCurrentDefault());

  auto config_result = ReportQueueConfiguration::Create(
      event_type, destination,
      base::BindRepeating([]() { return Status::StatusOK(); }), reserved_space);
  if (!config_result.has_value()) {
    LOG_WITH_STATUS(1, "ReportQueueConfiguration is invalid.", config_result);
    return;
  }

  // Asynchronously create and try to set ReportQueue.
  auto try_set_cb = base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindOnce(&TrySetReportQueue, std::move(success_cb)));
  base::ThreadPool::PostTask(
      FROM_HERE,
      base::BindOnce(ReportQueueProvider::CreateQueue,
                     std::move(config_result.value()), std::move(try_set_cb)));
}

// static
std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>
ReportQueueFactory::CreateSpeculativeReportQueue(EventType event_type,
                                                 Destination destination,
                                                 int64_t reserved_space) {
  CHECK(base::SequencedTaskRunner::HasCurrentDefault());

  auto config_result = ReportQueueConfiguration::Create(
      event_type, destination,
      base::BindRepeating([]() { return Status::StatusOK(); }), reserved_space);
  if (!config_result.has_value()) {
    DVLOG(1)
        << "Cannot initialize report queue. Invalid ReportQueueConfiguration: "
        << config_result.error();
    return std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>(
        nullptr, base::OnTaskRunnerDeleter(
                     base::SequencedTaskRunner::GetCurrentDefault()));
  }

  auto speculative_queue_result = ReportQueueProvider::CreateSpeculativeQueue(
      std::move(config_result.value()));
  if (!speculative_queue_result.has_value()) {
    DVLOG(1) << "Failed to create speculative queue: "
             << speculative_queue_result.error();
    return std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>(
        nullptr, base::OnTaskRunnerDeleter(
                     base::SequencedTaskRunner::GetCurrentDefault()));
  }

  return std::move(speculative_queue_result.value());
}
}  // namespace reporting

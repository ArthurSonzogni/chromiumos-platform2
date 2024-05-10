// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/top_sheriff.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>

namespace heartd {

TopSheriff::TopSheriff(base::OnceCallback<void()> quit_heartd_job,
                       HeartbeatManager* heartbeat_manager)
    : quit_heartd_job_(std::move(quit_heartd_job)),
      heartbeat_manager_(heartbeat_manager) {}

TopSheriff::~TopSheriff() = default;

void TopSheriff::OneShotWork() {
  // Asks the managed sheriffs get to work.
  for (const auto& sheriff : sheriffs) {
    sheriff->GetToWork();
  }

  // Run CleanUp() after two minutes from start up so that we can terminate
  // heartd earlier when there are no active jobs.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&TopSheriff::CleanUp, base::Unretained(this)),
      base::Minutes(2));
}

bool TopSheriff::HasShiftWork() {
  return true;
}

void TopSheriff::AdjustSchedule() {
  schedule_ = base::Hours(1);
}

void TopSheriff::ShiftWork() {
  CleanUp();
  for (const auto& sheriff : sheriffs) {
    sheriff->CleanUp();
  }
}

void TopSheriff::CleanUp() {
  if (heartbeat_manager_ && heartbeat_manager_->AnyHeartbeatTracker()) {
    return;
  }

  for (const auto& sheriff : sheriffs) {
    if (sheriff->IsWorking()) {
      return;
    }
  }

  LOG(INFO) << "There is no running jobs, stop heartd";
  std::move(quit_heartd_job_).Run();
}

void TopSheriff::AddSheriff(std::unique_ptr<Sheriff> sheriff) {
  sheriffs.push_back(std::move(sheriff));
}

}  // namespace heartd

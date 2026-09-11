// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/fake_browser_job.h"

#include <base/check.h>

#include "login_manager/fake_child_process.h"

namespace login_manager {

FakeBrowserJob::FakeBrowserJob(const std::string& name) : name_(name) {}

FakeBrowserJob::~FakeBrowserJob() = default;

void FakeBrowserJob::set_fake_child_process(
    std::unique_ptr<FakeChildProcess> fake) {
  fake_process_ = std::move(fake);
}

void FakeBrowserJob::set_schedule_exit(bool value) {
  schedule_exit_ = value;
}

bool FakeBrowserJob::IsGuestSession() {
  return false;
}

bool FakeBrowserJob::RunInBackground(
    base::OnceCallback<void(const siginfo_t&)> callback) {
  if (schedule_exit_) {
    DCHECK(fake_process_.get());
    fake_process_->ScheduleExit();
  }
  spawn_time_ = base::TimeTicks::Now();
  return running_ = true;
}

const std::string FakeBrowserJob::GetName() const {
  return name_;
}

pid_t FakeBrowserJob::CurrentPid() const {
  DCHECK(fake_process_.get());
  return (running_ ? fake_process_->pid() : -1);
}

std::optional<base::TimeTicks> FakeBrowserJob::GetSpawnTime() const {
  return spawn_time_;
}

void FakeBrowserJob::ClearPid() {
  running_ = false;
  spawn_time_.reset();
}

}  // namespace login_manager

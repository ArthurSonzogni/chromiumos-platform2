// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/fake_browser_job.h"

#include <base/check.h>

#include "login_manager/fake_child_process.h"

namespace login_manager {

FakeBrowserJob::FakeBrowserJob(const std::string& name) : name_(name) {}

FakeBrowserJob::~FakeBrowserJob() = default;

bool FakeBrowserJob::IsGuestSession() {
  return false;
}

bool FakeBrowserJob::RunInBackground(
    base::OnceCallback<void(const siginfo_t&)> callback) {
  if (schedule_exit_) {
    DCHECK(fake_process_.get());
    fake_process_->ScheduleExit();
  }
  return running_ = true;
}

const std::string FakeBrowserJob::GetName() const {
  return name_;
}

pid_t FakeBrowserJob::CurrentPid() const {
  DCHECK(fake_process_.get());
  return (running_ ? fake_process_->pid() : -1);
}

void FakeBrowserJob::ClearPid() {
  running_ = false;
}

}  // namespace login_manager

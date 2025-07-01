// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/vpd_process_impl.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/process/process_reaper.h>
#include <metrics/metrics_library.h>

namespace {

constexpr char kVpdUpdateMetric[] = "Enterprise.VpdUpdateStatus";

}  // namespace

namespace login_manager {

VpdProcessImpl::VpdProcessImpl(SystemUtils* system_utils,
                               brillo::ProcessReaper& process_reaper)
    : system_utils_(system_utils), process_reaper_(process_reaper) {
  DCHECK(system_utils_);
}

VpdProcessImpl::~VpdProcessImpl() {
  // Release dangling callback if it has.
  if (subprocess_) {
    pid_t pid = subprocess_->GetPid();
    if (pid >= 0) {
      process_reaper_->ForgetChild(pid);
    }
  }
}

void VpdProcessImpl::RequestJobExit(const std::string& reason) {
  if (subprocess_ && subprocess_->GetPid() > 0) {
    subprocess_->Kill(SIGTERM);
  }
}

void VpdProcessImpl::EnsureJobExit(base::TimeDelta timeout) {
  if (subprocess_) {
    if (subprocess_->GetPid() < 0) {
      subprocess_.reset();
      return;
    }
    if (!system_utils_->ProcessGroupIsGone(subprocess_->GetPid(), timeout)) {
      subprocess_->KillEverything(SIGABRT);
      DLOG(INFO) << "Child process was killed.";
    }
  }
}

bool VpdProcessImpl::RunInBackground(const KeyValuePairs& updates,
                                     CompletionCallback completion) {
  DUMP_WILL_BE_CHECK(!subprocess_ || subprocess_->GetPid() <= 0)
      << "Another subprocess is running";
  subprocess_.reset(new Subprocess(0 /*root*/, system_utils_));

  std::vector<std::string> argv = {
      // update_rw_vpd uses absl logging library, rather than the ones provided
      // by libchrome/libbrillo, which outputs the logs to stderr, rather than
      // syslog. Use syslog-cat to redirect it to syslog so that errors can
      // be captured.
      "/usr/sbin/syslog-cat", "--identifier=update_rw_vpd",
      "--severity_stderr=error", "--", "/usr/sbin/update_rw_vpd"};
  for (const auto& entry : updates) {
    argv.push_back(entry.first);
    argv.push_back(entry.second);
  }

  if (!subprocess_->ForkAndExec(argv, std::vector<std::string>())) {
    subprocess_.reset();
    // The caller remains responsible for running |completion|.
    return false;
  }

  // |completion_| will be run when the job exits.
  process_reaper_->WatchForChild(
      FROM_HERE, subprocess_->GetPid(),
      base::BindOnce(&VpdProcessImpl::HandleExit, weak_factory_.GetWeakPtr(),
                     std::move(completion)));
  return true;
}

void VpdProcessImpl::HandleExit(CompletionCallback callback,
                                const siginfo_t& info) {
  CHECK(subprocess_);
  CHECK_GE(subprocess_->GetPid(), 0);
  CHECK_EQ(subprocess_->GetPid(), info.si_pid);

  subprocess_.reset();
  MetricsLibrary metrics;
  metrics.SendSparseToUMA(kVpdUpdateMetric, info.si_status);

  const bool success = (info.si_status == 0);
  LOG_IF(ERROR, !success) << "Failed to update VPD, code = " << info.si_status;

  // Reset the completion to ensure we won't call it again.
  if (callback) {
    std::move(callback).Run(success);
  }
}

}  // namespace login_manager

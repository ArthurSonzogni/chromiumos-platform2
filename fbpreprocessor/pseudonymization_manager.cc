// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/pseudonymization_manager.h"

#include <set>
#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>

#include "fbpreprocessor/constants.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/metrics.h"
#include "fbpreprocessor/output_manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace {
constexpr base::TimeDelta kMaxProcessedInterval = base::Minutes(30);
}  // namespace

namespace fbpreprocessor {

PseudonymizationManager::PseudonymizationManager(Manager* manager)
    : base_dir_(kDaemonStorageRoot), manager_(manager) {
  CHECK(manager_->session_state_manager());
  manager_->session_state_manager()->AddObserver(this);
}

PseudonymizationManager::~PseudonymizationManager() {
  if (manager_->session_state_manager()) {
    manager_->session_state_manager()->RemoveObserver(this);
  }
}

bool PseudonymizationManager::StartPseudonymization(
    const FirmwareDump& fw_dump) {
  VLOG(kLocalDebugVerbosity) << __func__;
  // For the MVP we're not pseudonymizing, so the pseudonymization operation
  // is merely a move which is ~immediate. No need to handle multiple concurrent
  // long-running operations for now.
  if (user_root_dir_.empty()) {
    LOG(ERROR) << "Can't start pseudonymization without output directory.";
    if (!fw_dump.Delete()) {
      LOG(ERROR) << "Failed to delete input firmware dump.";
    }
    return false;
  }

  if (!RateLimitingAllowsNewPseudonymization(fw_dump.type())) {
    LOG(INFO) << "Too many recent pseudonymizations, rejecting the current "
                 "request.";
    VLOG(kLocalOnlyDebugVerbosity)
        << "Rejected request for file" << fw_dump.DumpFile();
    if (!fw_dump.Delete()) {
      LOG(ERROR) << "Failed to delete input firmware dump.";
    }
    return false;
  }
  FirmwareDump output(
      user_root_dir_.Append(kProcessedDirectory).Append(fw_dump.BaseName()),
      fw_dump.type());
  manager_->metrics().SendPseudonymizationFirmwareType(fw_dump.type());
  if (manager_->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(&PseudonymizationManager::DoNoOpPseudonymization,
                         weak_factory_.GetWeakPtr(), fw_dump, output))) {
    // We successfully posted the pseudonymization task, keep track of the
    // start timestamp for future rate limit checks.
    base::Time now = base::Time::Now();
    recently_processed_lock_.Acquire();
    // Since we checked the rate limit earlier but only add the operation now,
    // there is a small time window where another request would be allowed
    // even if we already hit the rate limit. That is acceptable since in
    // practice firmware dumps are not generated that frequenly by the rest
    // of the stack (every few seconds at most). The feedback report creation
    // tool will also limit how many firmware dumps are added, so potentially
    // creating 1 extra firmware dump is tolerable.
    recently_processed_.insert(FirmwareDumpTimestamp(fw_dump.type(), now));
    recently_processed_lock_.Release();
  } else {
    LOG(ERROR) << "Failed to post pseudonymization task.";
    if (!fw_dump.Delete()) {
      LOG(ERROR) << "Failed to delete input firmware dump.";
    }
    manager_->metrics().SendPseudonymizationResult(
        fw_dump.type(),
        fbpreprocessor::Metrics::PseudonymizationResult::kFailedToStart);
    return false;
  }
  return true;
}

void PseudonymizationManager::OnUserLoggedIn(const std::string& user_dir) {
  LOG(INFO) << "User logged in.";
  user_root_dir_.clear();
  if (user_dir.empty()) {
    LOG(ERROR) << "No user directory defined.";
    return;
  }
  user_root_dir_ = base_dir_.Append(user_dir);
  ResetRateLimiter();
}

void PseudonymizationManager::OnUserLoggedOut() {
  LOG(INFO) << "User logged out.";
  user_root_dir_.clear();
  ResetRateLimiter();
}

void PseudonymizationManager::DoNoOpPseudonymization(
    const FirmwareDump& input, const FirmwareDump& output) const {
  Result result = Result::kSuccess;
  LOG(INFO) << "Pseudonymizing in progress.";
  VLOG(kLocalOnlyDebugVerbosity) << "Pseudonymizing " << input;
  if (!base::Move(input.DumpFile(), output.DumpFile())) {
    LOG(ERROR) << "Failed to move file to destination.";
    result = Result::kNoOpFailedToMove;
  }
  OnPseudonymizationComplete(input, output, result);
}

void PseudonymizationManager::OnPseudonymizationComplete(
    const FirmwareDump& input,
    const FirmwareDump& output,
    Result result) const {
  bool success = result == Result::kSuccess;
  LOG(INFO) << "Pseudonymization completed" << (success ? " " : " un")
            << "successfully.";
  VLOG(kLocalOnlyDebugVerbosity) << "Completed pseudonymization of " << input;
  manager_->metrics().SendPseudonymizationResult(input.type(),
                                                 ConvertToMetrics(result));
  if (success) {
    CHECK(manager_->output_manager());
    manager_->output_manager()->AddFirmwareDump(output);
  } else {
    if (!output.Delete()) {
      LOG(ERROR) << "Failed to delete output firmware dump after "
                 << "pseudonymization failure.";
    }
  }
  if (!input.Delete()) {
    LOG(ERROR) << "Failed to delete input firmware dump after "
               << "pseudonymization.";
  }
}

bool PseudonymizationManager::RateLimitingAllowsNewPseudonymization(
    FirmwareDump::Type type) {
  base::Time now = base::Time::Now();
  int dump_count = 0;

  // Erase all the pseudonymizations that happened more than
  // |kMaxProcessedInterval| ago.
  recently_processed_lock_.Acquire();
  for (auto it = recently_processed_.begin();
       it != recently_processed_.end();) {
    if ((now - it->timestamp) > kMaxProcessedInterval) {
      it = recently_processed_.erase(it);
    } else {
      if (it->type == type) {
        dump_count++;
      }
      ++it;
    }
  }
  recently_processed_lock_.Release();

  // If fewer than |kMaxProcessedDumps| pseudonymizations are left it means
  // we're not hitting the rate limit.
  return dump_count < kMaxProcessedDumps;
}

void PseudonymizationManager::ResetRateLimiter() {
  recently_processed_lock_.Acquire();
  recently_processed_.clear();
  recently_processed_lock_.Release();
}

// static
Metrics::PseudonymizationResult PseudonymizationManager::ConvertToMetrics(
    Result result) {
  switch (result) {
    case Result::kUnknown:
      return Metrics::PseudonymizationResult::kUnknown;
    case Result::kSuccess:
      return Metrics::PseudonymizationResult::kSuccess;
    case Result::kFailedToStart:
      return Metrics::PseudonymizationResult::kFailedToStart;
    case Result::kNoOpFailedToMove:
      return Metrics::PseudonymizationResult::kNoOpFailedToMove;
  }
}

}  // namespace fbpreprocessor

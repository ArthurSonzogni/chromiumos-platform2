// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/pseudonymization_manager.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/input_manager.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/output_manager.h"
#include "fbpreprocessor/session_state_manager.h"
#include "fbpreprocessor/storage.h"

namespace fbpreprocessor {

PseudonymizationManager::PseudonymizationManager(Manager* manager)
    : manager_(manager) {
  manager_->session_state_manager()->AddObserver(this);
}

PseudonymizationManager::~PseudonymizationManager() {
  if (manager_->session_state_manager())
    manager_->session_state_manager()->RemoveObserver(this);
}

void PseudonymizationManager::StartPseudonymization(
    const FirmwareDump& fw_dump) {
  // For the MVP we're not pseudonymizing, so the pseudonymization operation
  // is merely a move which is ~immediate. No need to handle multiple concurrent
  // long-running operations for now.
  if (user_root_dir_.empty()) {
    LOG(ERROR) << "Can't start pseudonymization without output directory.";
    return;
  }
  FirmwareDump output(
      user_root_dir_.Append(kProcessedDirectory).Append(fw_dump.BaseName()));
  if (base::SequencedTaskRunner::HasCurrentDefault()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&PseudonymizationManager::DoNoOpPseudonymization,
                       weak_factory_.GetWeakPtr(), fw_dump, output));
  } else {
    LOG(ERROR) << "No task runner.";
  }
}

void PseudonymizationManager::OnUserLoggedIn(const std::string& user_dir) {
  LOG(INFO) << "User logged in.";
  user_root_dir_.clear();
  if (user_dir.empty()) {
    LOG(ERROR) << "No user directory defined.";
    return;
  }
  user_root_dir_ = base::FilePath(kDaemonStorageRoot).Append(user_dir);
}
void PseudonymizationManager::OnUserLoggedOut() {
  LOG(INFO) << "User logged out.";
  user_root_dir_.clear();
}

void PseudonymizationManager::DoNoOpPseudonymization(
    const FirmwareDump& input, const FirmwareDump& output) {
  bool success = true;
  LOG(INFO) << "Pseudonymizing " << input;
  if (!base::Move(input.DumpFile(), output.DumpFile())) {
    LOG(ERROR) << "Failed to move file to destination.";
    success = false;
  }
  OnPseudonymizationComplete(input, output, success);
}

void PseudonymizationManager::OnPseudonymizationComplete(
    const FirmwareDump& input, const FirmwareDump& output, bool success) {
  // TODO(b/307593542): remove filenames from logs.
  LOG(INFO) << "Completed pseudonymization of " << input
            << (success ? " " : " un") << "successfully.";
  if (success) {
    manager_->output_manager()->AddNewFile(output);
  }
  manager_->input_manager()->DeleteFirmwareDump(input);
}

}  // namespace fbpreprocessor

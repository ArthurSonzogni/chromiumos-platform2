// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/input_manager.h"

#include <string>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>

#include "fbpreprocessor/constants.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/pseudonymization_manager.h"
#include "fbpreprocessor/session_state_manager.h"
#include "fbpreprocessor/storage.h"

namespace fbpreprocessor {

InputManager::InputManager(Manager* manager)
    : base_dir_(kDaemonStorageRoot), manager_(manager) {
  CHECK(manager_->session_state_manager());
  manager_->session_state_manager()->AddObserver(this);
}

InputManager::~InputManager() {
  if (manager_->session_state_manager())
    manager_->session_state_manager()->RemoveObserver(this);
}

void InputManager::OnUserLoggedIn(const std::string& user_dir) {
  LOG(INFO) << "User logged in.";
  user_root_dir_.clear();
  if (user_dir.empty()) {
    LOG(ERROR) << "No user directory defined.";
    return;
  }
  user_root_dir_ = base_dir_.Append(user_dir);
  DeleteAllFiles();
}

void InputManager::OnUserLoggedOut() {
  LOG(INFO) << "User logged out.";
  user_root_dir_.clear();
}

bool InputManager::OnNewFirmwareDump(const FirmwareDump& fw_dump) const {
  if (!base::PathExists(fw_dump.DumpFile())) {
    LOG(ERROR) << "Can't find firmware dump on disk.";
    VLOG(kLocalOnlyDebugVerbosity)
        << "Firmware dump doesn't exist: " << fw_dump.DumpFile().value();
    return false;
  }
  if (!manager_->FirmwareDumpsAllowed()) {
    // The feature is disabled, but firmware dumps were created anyway.
    // Delete those firmware dumps.
    LOG(INFO) << "Feature disabled, deleting firmware dump.";
    if (!fw_dump.Delete()) {
      LOG(ERROR) << "Failed to delete firmware dump.";
    }
    return false;
  }
  if (manager_->pseudonymization_manager()) {
    if (!manager_->pseudonymization_manager()->StartPseudonymization(fw_dump)) {
      LOG(ERROR) << "Failed to start pseudonymization.";
      return false;
    }
  }
  return true;
}

void InputManager::DeleteAllFiles() const {
  VLOG(kLocalDebugVerbosity) << __func__;
  base::FileEnumerator files(user_root_dir_.Append(kInputDirectory),
                             false /* recursive */,
                             base::FileEnumerator::FILES);
  files.ForEach([](const base::FilePath& path) {
    VLOG(kLocalOnlyDebugVerbosity) << "Cleaning up file " << path.BaseName();
    if (!brillo::DeleteFile(path)) {
      LOG(ERROR) << __func__ << ": File deletion failure detected.";
      VLOG(kLocalOnlyDebugVerbosity) << "Failed to delete " << path.BaseName();
    }
  });
}

}  // namespace fbpreprocessor

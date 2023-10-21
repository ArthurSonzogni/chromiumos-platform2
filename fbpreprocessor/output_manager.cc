// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/output_manager.h"

#include <forward_list>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <brillo/files/file_util.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/platform_features_client.h"
#include "fbpreprocessor/storage.h"

namespace {
void DeleteFirmwareDump(const fbpreprocessor::FirmwareDump& fw_dump) {
  fw_dump.Delete();
}
}  // namespace

namespace fbpreprocessor {

OutputManager::OutputManager(Manager* manager)
    : default_expiration_(
          base::Seconds(manager->default_file_expiration_in_secs())),
      manager_(manager) {
  manager_->session_state_manager()->AddObserver(this);
  manager_->platform_features()->AddObserver(this);
}

OutputManager::~OutputManager() {
  if (manager_->session_state_manager())
    manager_->session_state_manager()->RemoveObserver(this);
  if (manager_->platform_features()) {
    manager_->platform_features()->RemoveObserver(this);
  }
}

void OutputManager::OnUserLoggedIn(const std::string& user_dir) {
  LOG(INFO) << "User logged in.";
  user_root_dir_.clear();
  if (user_dir.empty()) {
    LOG(ERROR) << "No user directory defined.";
    return;
  }
  user_root_dir_ = base::FilePath(kDaemonStorageRoot).Append(user_dir);
  DeleteAllFiles();
}

void OutputManager::OnUserLoggedOut() {
  LOG(INFO) << "User logged out.";
  user_root_dir_.clear();
}

void OutputManager::OnFeatureChanged(bool allowed) {
  if (!allowed) {
    DeleteAllFiles();
  }
}

void OutputManager::AddNewFile(const FirmwareDump& fw_dump,
                               const base::TimeDelta& expiration) {
  if (!manager_->platform_features()->FirmwareDumpsAllowed()) {
    // The value of the Finch flag may have been changed during the
    // pseudonymization process, delete the files here.
    LOG(INFO) << "Feature disabled, deleting firmware dump.";
    fw_dump.Delete();
    return;
  }
  // TODO(b/307593542): remove filenames from logs.
  LOG(INFO) << "File " << fw_dump << " will expire in " << expiration;
  base::Time now = base::Time::Now();
  OutputFile file(fw_dump, now + expiration);
  files_lock_.Acquire();
  files_.insert(file);
  RestartExpirationTask(now);
  files_lock_.Release();
}

void OutputManager::AddNewFile(const FirmwareDump& fw_dump) {
  AddNewFile(fw_dump, default_expiration_);
}

std::forward_list<FirmwareDump> OutputManager::AvailableDumps() {
  std::forward_list<FirmwareDump> dumps;
  if (!manager_->platform_features()->FirmwareDumpsAllowed()) {
    return dumps;
  }
  files_lock_.Acquire();
  for (auto file : files_) {
    dumps.push_front(file.fw_dump());
  }
  files_lock_.Release();
  return dumps;
}

void OutputManager::RestartExpirationTask(const base::Time& now) {
  if (!files_.empty()) {
    expiration_timer_.Stop();
    base::TimeDelta delay = files_.begin()->expiration() - now;
    // If the difference between now and the first expiration date is negative,
    // it means that some files have already expired. In that case set a 0 delay
    // to trigger the call to |DeleteExpiredFiles()| immediately.
    if (delay < base::TimeDelta()) {
      delay = base::TimeDelta();
    }
    expiration_timer_.Start(FROM_HERE, delay,
                            base::BindOnce(&OutputManager::OnExpiredFile,
                                           weak_factory_.GetWeakPtr()));
  }
}

void OutputManager::OnExpiredFile() {
  files_lock_.Acquire();
  base::Time now = base::Time::Now();
  for (auto it = files_.begin(); it != files_.end();) {
    if (it->expiration() <= now) {
      // TODO(b/307593542): remove filenames from logs.
      LOG(INFO) << "Deleting file " << it->fw_dump();
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&DeleteFirmwareDump, it->fw_dump()));
      it = files_.erase(it);
    } else {
      ++it;
    }
  }
  RestartExpirationTask(now);
  files_lock_.Release();
}

void OutputManager::DeleteAllManagedFiles() {
  files_lock_.Acquire();
  for (auto f : files_) {
    if (!f.fw_dump().Delete()) {
      LOG(ERROR) << "Failed to delete file.";
    }
  }
  files_.clear();
  files_lock_.Release();
}

void OutputManager::DeleteAllFiles() {
  DeleteAllManagedFiles();
  base::FileEnumerator files(user_root_dir_.Append(kProcessedDirectory),
                             false /* recursive */,
                             base::FileEnumerator::FILES);
  files.ForEach([](const base::FilePath& path) {
    // TODO(b/307593542): remove filenames from logs.
    LOG(INFO) << "Cleaning up file " << path.BaseName();
    if (!brillo::DeleteFile(path)) {
      LOG(ERROR) << "Failed to delete file.";
    }
  });
}

}  // namespace fbpreprocessor

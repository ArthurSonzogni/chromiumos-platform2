// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/output_manager.h"

#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <brillo/files/file_util.h>

#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/storage.h"

namespace fbpreprocessor {

OutputManager::OutputManager(Manager* manager)
    : default_expiration_(
          base::Seconds(manager->default_file_expiration_in_secs())),
      manager_(manager) {
  manager_->session_state_manager()->AddObserver(this);
}

OutputManager::~OutputManager() {
  if (manager_->session_state_manager())
    manager_->session_state_manager()->RemoveObserver(this);
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

void OutputManager::AddNewFile(const base::FilePath& path,
                               const base::TimeDelta& expiration) {
  // TODO(b/307593542): remove filenames from logs.
  LOG(INFO) << "File " << path << " will expire in " << expiration;
  base::Time now = base::Time::Now();
  OutputFile file(path, now + expiration);
  files_lock_.Acquire();
  files_.insert(file);
  RestartExpirationTask(now);
  files_lock_.Release();
}

void OutputManager::AddNewFile(const base::FilePath& path) {
  AddNewFile(path, default_expiration_);
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
                            base::BindOnce(&OutputManager::DeleteExpiredFiles,
                                           weak_factory_.GetWeakPtr()));
  }
}

void OutputManager::DeleteExpiredFiles() {
  files_lock_.Acquire();
  base::Time now = base::Time::Now();
  for (auto it = files_.begin(); it != files_.end();) {
    if (it->expiration() <= now) {
      // TODO(b/307593542): remove filenames from logs.
      LOG(INFO) << "Deleting file " << it->path().BaseName();
      if (!brillo::DeleteFile(it->path())) {
        LOG(ERROR) << "Failed to delete file.";
      }
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
    if (!brillo::DeleteFile(f.path())) {
      LOG(ERROR) << "Failed to delete file.";
    }
  }
  files_.clear();
  files_lock_.Release();
}

void OutputManager::DeleteAllFiles() {
  DeleteAllManagedFiles();
  base::FileEnumerator e(user_root_dir_.Append(kProcessedDirectory), false,
                         base::FileEnumerator::FILES);
  e.ForEach([](const base::FilePath& path) {
    // TODO(b/307593542): remove filenames from logs.
    LOG(INFO) << "Cleaning up file " << path;
    if (!brillo::DeleteFile(path)) {
      LOG(ERROR) << "Failed to delete file.";
    }
  });
}

}  // namespace fbpreprocessor

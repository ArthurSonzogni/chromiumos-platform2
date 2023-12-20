// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/output_manager.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/files/file_util.h>
#include <dbus/dbus-protocol.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

#include "fbpreprocessor/constants.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/platform_features_client.h"
#include "fbpreprocessor/storage.h"

namespace {
void DeleteFirmwareDump(const fbpreprocessor::FirmwareDump& fw_dump,
                        std::string_view reason) {
  LOG(INFO) << "Deleting WiFi dump file triggered by: " << reason;
  VLOG(fbpreprocessor::kLocalOnlyDebugVerbosity) << "Deleting file " << fw_dump;
  if (!fw_dump.Delete()) {
    LOG(ERROR) << "Failed to delete firmware dump.";
  }
}
}  // namespace

namespace fbpreprocessor {

OutputManager::OutputManager(Manager* manager)
    : expire_after_(base::Seconds(manager->default_file_expiration_in_secs())),
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
  VLOG(kLocalDebugVerbosity) << __func__;
  if (!allowed) {
    DeleteAllFiles();
  }
}

void OutputManager::AddFirmwareDump(const FirmwareDump& fw_dump) {
  VLOG(kLocalDebugVerbosity) << __func__;
  if (!manager_->FirmwareDumpsAllowed()) {
    // The value of the Finch flag or the policy may have been changed during
    // the pseudonymization process, delete the files here.
    LOG(INFO) << "Feature disabled, deleting firmware dump.";
    DeleteFirmwareDump(fw_dump, __func__);
    return;
  }
  base::Time now = base::Time::Now();
  OutputFile file(fw_dump, now + expire_after_);
  files_lock_.Acquire();
  files_.insert(file);
  RestartExpirationTask(now);
  files_lock_.Release();
}

void OutputManager::GetDebugDumps(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<DebugDumps>>
        response) {
  VLOG(kLocalDebugVerbosity) << __func__;
  if (!manager_->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(&OutputManager::GetAllAvailableDebugDumps,
                         weak_factory_.GetWeakPtr(), std::move(response)))) {
    LOG(ERROR) << "Failed to post GetDebugDumpsAsync task.";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED,
                             "Failed to post task for GetDebugDumps request.");
  }
}

void OutputManager::GetAllAvailableDebugDumps(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<DebugDumps>>
        response) {
  VLOG(kLocalDebugVerbosity) << __func__;
  DebugDumps out_DebugDumps;
  if (!manager_->FirmwareDumpsAllowed()) {
    response->Return(out_DebugDumps);
    return;
  }

  files_lock_.Acquire();
  for (auto file : files_) {
    // TODO(b/308984163): Add the metadata information to
    // fbpreprocessor::FirmwareDump instead of hardcoding it here.
    auto dump = file.fw_dump();
    auto debug_dump = out_DebugDumps.add_dump();
    debug_dump->set_type(DebugDump::WIFI);
    auto wifi_dump = debug_dump->mutable_wifi_dump();
    wifi_dump->set_dmpfile(dump.DumpFile().value());
    wifi_dump->set_state(WiFiDump::RAW);
    wifi_dump->set_vendor(WiFiDump::IWLWIFI);
  }
  files_lock_.Release();
  response->Return(out_DebugDumps);
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
      // Run the file deletion task asynchronously to avoid blocking on I/O
      // while we're holding the lock.
      manager_->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(&DeleteFirmwareDump, it->fw_dump(), "scheduled task"));
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
    DeleteFirmwareDump(f.fw_dump(), __func__);
  }
  files_.clear();
  files_lock_.Release();
}

void OutputManager::DeleteAllFiles() {
  VLOG(kLocalDebugVerbosity) << __func__;
  DeleteAllManagedFiles();
  base::FileEnumerator files(user_root_dir_.Append(kProcessedDirectory),
                             false /* recursive */,
                             base::FileEnumerator::FILES);
  files.ForEach([](const base::FilePath& path) {
    VLOG(kLocalOnlyDebugVerbosity) << "Cleaning up file " << path.BaseName();
    if (!brillo::DeleteFile(path)) {
      LOG(ERROR) << "Failed to delete file.";
    }
  });
}

}  // namespace fbpreprocessor

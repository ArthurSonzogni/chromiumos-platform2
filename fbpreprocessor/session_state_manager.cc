// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/session_state_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <session_manager/dbus-proxies.h>

#include "fbpreprocessor/storage.h"

namespace {
constexpr char kSessionStateStarted[] = "started";
constexpr char kSessionStateStopped[] = "stopped";
}  // namespace

namespace fbpreprocessor {

SessionStateManager::SessionStateManager(dbus::Bus* bus)
    : session_manager_proxy_(
          std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus)) {
  session_manager_proxy_->RegisterSessionStateChangedSignalHandler(
      base::BindRepeating(&SessionStateManager::OnSessionStateChanged,
                          weak_factory.GetWeakPtr()),
      base::BindOnce(&SessionStateManager::OnSignalConnected,
                     weak_factory.GetWeakPtr()));
}

void SessionStateManager::OnSessionStateChanged(const std::string& state) {
  LOG(INFO) << "Session state changed to " << state;

  if (state == kSessionStateStarted) {
    if (!primary_user_hash_.empty()) {
      LOG(INFO) << "Primary user already exists. Not updating primary user.";
      return;
    }

    if (UpdatePrimaryUser()) {
      for (auto& observer : observers_) {
        observer.OnUserLoggedIn(primary_user_hash_);
      }
    }
  } else if (state == kSessionStateStopped) {
    primary_user_hash_.clear();
    for (auto& observer : observers_) {
      observer.OnUserLoggedOut();
    }
  }
}

void SessionStateManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SessionStateManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::optional<std::pair<std::string, std::string>>
SessionStateManager::RetrievePrimaryUser() {
  brillo::ErrorPtr error;
  std::string username;
  std::string sanitized_username;
  if (!session_manager_proxy_->RetrievePrimarySession(
          &username, &sanitized_username, &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve primary session: " << error->GetMessage();
    return std::nullopt;
  }

  return std::make_pair(username, sanitized_username);
}

bool SessionStateManager::UpdatePrimaryUser() {
  auto primary_user = RetrievePrimaryUser();

  if (!primary_user.has_value()) {
    LOG(ERROR) << "Error while retrieving primary user.";
    return false;
  }

  if (primary_user->first.empty() || primary_user->second.empty()) {
    LOG(INFO) << "Primary user does not exist.";
    return false;
  }

  primary_user_.assign(primary_user->first);
  primary_user_hash_.assign(primary_user->second);
  LOG(INFO) << "Primary user updated.";

  if (!CreateUserDirectories()) {
    LOG(ERROR) << "Failed to create input/output directories.";
  }

  return true;
}

bool SessionStateManager::RefreshPrimaryUser() {
  std::string old_primary_user_hash = primary_user_hash_;
  primary_user_hash_.clear();

  bool update_result = UpdatePrimaryUser();

  if (old_primary_user_hash.empty() && !primary_user_hash_.empty()) {
    for (auto& observer : observers_) {
      observer.OnUserLoggedIn(primary_user_hash_);
    }
  } else if (!old_primary_user_hash.empty() && primary_user_hash_.empty()) {
    for (auto& observer : observers_) {
      observer.OnUserLoggedOut();
    }
  }

  return update_result;
}

bool SessionStateManager::CreateUserDirectories() {
  bool success = true;
  if (primary_user_hash_.empty()) {
    LOG(ERROR) << "Can't create input/output directories without daemon store.";
    return false;
  }
  base::FilePath root_dir =
      base::FilePath(kDaemonStorageRoot).Append(primary_user_hash_);
  base::File::Error error;
  if (!base::CreateDirectoryAndGetError(root_dir.Append(kInputDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create input directory: "
               << base::File::ErrorToString(error);
    success = false;
  }
  if (!base::CreateDirectoryAndGetError(root_dir.Append(kProcessedDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create output directory: "
               << base::File::ErrorToString(error);
    success = false;
  }

  return success;
}

void SessionStateManager::OnSignalConnected(const std::string& interface_name,
                                            const std::string& signal_name,
                                            bool success) {
  if (!success)
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
  if (success) {
    LOG(INFO) << "Connected to signal " << signal_name << " of interface "
              << interface_name;
  }
}

}  // namespace fbpreprocessor

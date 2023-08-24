// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/session_state_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <dbus/bus.h>
#include <dbus/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "fbpreprocessor/storage.h"

namespace fbpreprocessor {

SessionStateManager::SessionStateManager(dbus::Bus* bus) {
  session_manager_proxy_ = bus->GetObjectProxy(
      login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));

  session_manager_proxy_->ConnectToSignal(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionStateChangedSignal,
      base::BindRepeating(&SessionStateManager::OnSessionStateChanged,
                          weak_factory.GetWeakPtr()),
      base::BindOnce(&SessionStateManager::OnSignalConnected,
                     weak_factory.GetWeakPtr()));
}

void SessionStateManager::OnSessionStateChanged(dbus::Signal* signal) {
  CHECK(signal != nullptr) << "Invalid OnSessionStateChanged signal.";
  dbus::MessageReader signal_reader(signal);
  std::string state;

  CHECK(signal_reader.PopString(&state));
  LOG(INFO) << "Session state changed to " << state;

  if (state == dbus_constants::kSessionStateStarted) {
    if (!primary_user_hash_.empty()) {
      LOG(INFO) << "Primary user already exists. Not updating primary user.";
      return;
    }

    if (UpdatePrimaryUser()) {
      for (auto& observer : observers_) {
        observer.OnUserLoggedIn(primary_user_hash_);
      }
    }
  } else if (state == dbus_constants::kSessionStateStopped) {
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
  dbus::Error error;
  std::string sanitized_username;

  dbus::MethodCall method_call(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionManagerRetrievePrimarySession);

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> dbus_response =
      session_manager_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);

  if (!dbus_response.has_value()) {
    dbus::Error error = std::move(dbus_response.error());
    if (error.IsValid()) {
      std::string error_name = error.name();
      LOG(ERROR) << "Calling "
                 << login_manager::kSessionManagerRetrievePrimarySession
                 << " from " << login_manager::kSessionManagerInterface
                 << " interface finished with " << error_name << " error.";

      if (error_name == dbus_constants::kDBusErrorNoReply) {
        LOG(ERROR) << "Timeout while getting primary session.";
      } else if (error_name == dbus_constants::kDBusErrorServiceUnknown) {
        LOG(ERROR) << "Can't find " << login_manager::kSessionManagerServiceName
                   << " service. Maybe session_manager is not running?";
      } else {
        LOG(ERROR) << "Error details: " << error.message();
      }
      return std::nullopt;
    }
  }

  std::unique_ptr<dbus::Response> response = std::move(dbus_response.value());
  if (!response.get()) {
    LOG(ERROR) << "Cannot retrieve username for primary session.";
    return std::nullopt;
  }

  dbus::MessageReader response_reader(response.get());
  std::string username;
  if (!response_reader.PopString(&username)) {
    LOG(ERROR) << "Primary session username bad format.";
    return std::nullopt;
  }
  if (!response_reader.PopString(&sanitized_username)) {
    LOG(ERROR) << "Primary session sanitized username bad format.";
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

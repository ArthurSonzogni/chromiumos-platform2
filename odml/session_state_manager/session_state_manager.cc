// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/session_state_manager/session_state_manager.h"

#include <sys/types.h>

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <session_manager/dbus-proxies.h>

namespace odml {

namespace {

constexpr char kSessionStateStarted[] = "started";
constexpr char kSessionStateStopped[] = "stopped";

}  // namespace

// Production calls this.
SessionStateManager::SessionStateManager(dbus::Bus* bus)
    : SessionStateManager(
          std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus)) {}

// Called by the production constructor. Also interstitial to mock the proxy.
SessionStateManager::SessionStateManager(
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager_proxy)
    : session_manager_proxy_(std::move(session_manager_proxy)) {
  session_manager_proxy_->RegisterSessionStateChangedSignalHandler(
      base::BindRepeating(&SessionStateManager::OnSessionStateChanged,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&SessionStateManager::OnSignalConnected,
                     weak_factory_.GetWeakPtr()));
}

void SessionStateManager::OnSessionStateChanged(const std::string& state) {
  LOG(INFO) << "Session state changed to " << state;

  if (state == kSessionStateStarted) {
    RefreshPrimaryUser();
  } else if (state == kSessionStateStopped) {
    if (!primary_user_.has_value()) {
      return;
    }
    HandlUserLogout();
    primary_user_.reset();
  }
}

void SessionStateManager::HandlUserLogin(const User& user) {
  for (auto& observer : observers_) {
    observer.OnUserLoggedIn(user);
  }
}

void SessionStateManager::HandlUserLogout() {
  for (auto& observer : observers_) {
    observer.OnUserLoggedOut();
  }
}

void SessionStateManager::AddObserver(
    SessionStateManagerInterface::Observer* observer) {
  observers_.AddObserver(observer);
}

void SessionStateManager::RemoveObserver(
    SessionStateManagerInterface::Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::optional<SessionStateManager::User>
SessionStateManager::RetrievePrimaryUser() {
  brillo::ErrorPtr error;
  std::string username;
  std::string sanitized_username;
  if (!session_manager_proxy_->RetrievePrimarySession(
          &username, &sanitized_username, &error)) {
    std::string error_msg("unknown");
    if (error.get()) {
      error_msg = error->GetMessage();
    }
    LOG(ERROR) << "Failed to retrieve primary session: " << error_msg;
    return std::nullopt;
  }
  return User{.name = username, .hash = sanitized_username};
}

bool SessionStateManager::UpdatePrimaryUser() {
  std::optional<SessionStateManager::User> primary_user = RetrievePrimaryUser();

  // Failed to get primary owner.
  if (!primary_user.has_value()) {
    return false;
  }

  // No primary user logged in.
  if (primary_user->name.empty() || primary_user->hash.empty()) {
    primary_user_.reset();
    return true;
  }

  // Primary owner exists.
  primary_user_ = std::move(primary_user);
  return true;
}

bool SessionStateManager::RefreshPrimaryUser() {
  std::optional<User> old_primary_user = primary_user_;

  if (!UpdatePrimaryUser()) {
    LOG(WARNING) << "Unable to update primary user";
    return false;
  }

  if (!old_primary_user.has_value() && primary_user_.has_value()) {
    HandlUserLogin(*primary_user_);
  } else if (old_primary_user.has_value() && !primary_user_.has_value()) {
    HandlUserLogout();
  }
  return true;
}

void SessionStateManager::OnSignalConnected(const std::string& interface_name,
                                            const std::string& signal_name,
                                            bool success) const {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
    return;
  }
  LOG(INFO) << "Connected to signal " << signal_name << " of interface "
            << interface_name;
}

}  // namespace odml

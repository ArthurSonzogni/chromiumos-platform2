// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_SESSION_STATE_MANAGER_H_
#define FBPREPROCESSOR_SESSION_STATE_MANAGER_H_

#include <memory>
#include <string>
#include <utility>

#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <bindings/cloud_policy.pb.h>
#include <session_manager/dbus-proxies.h>

#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

class SessionStateManagerInterface {
 public:
  // Interface for observing session state changes. Objects that want to be
  // notified when the user logs in/out can add themselves to the list of
  // observers.
  class Observer {
   public:
    // Called when user was logged in. |user_dir| argument contains the path
    // to the daemon store where files can be read/written.
    virtual void OnUserLoggedIn(const std::string& user_dir) = 0;

    // Called when user was logged out.
    virtual void OnUserLoggedOut() = 0;

    virtual ~Observer() = default;
  };

  // Adds and removes the observer.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  virtual ~SessionStateManagerInterface() = default;
};

class SessionStateManager : public SessionStateManagerInterface {
 public:
  explicit SessionStateManager(Manager* manager, dbus::Bus* bus);
  SessionStateManager(const SessionStateManager&) = delete;
  SessionStateManager& operator=(const SessionStateManager&) = delete;
  ~SessionStateManager() override = default;

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  bool RefreshPrimaryUser();

  // Returns true if the user is allowed to include firmware dumps in feedback
  // reports, false otherwise.
  bool FirmwareDumpsAllowedByPolicy() const;

 private:
  void OnSessionStateChanged(const std::string& state);

  // Query session manager for the current primary user. Returns std::nullopt
  // when there was an error while getting primary user.
  // If successful, the first member of the pair is the username and the second
  // member is the sanitized username which is used for the home directory.
  std::optional<std::pair<std::string, std::string>> RetrievePrimaryUser();

  // Updates primary user internally.
  bool UpdatePrimaryUser();

  // Query session manager to know all the sessions that are currently active.
  // We keep track of the number of active sessions to ensure that only the
  // primary user is logged in.
  bool UpdateActiveSessions();

  // Retrieve the value of the UserFeedbackWithLowLevelDebugDataAllowed policy.
  bool UpdatePolicy();

  // Extract the value of the UserFeedbackWithLowLevelDebugDataAllowed policy
  // when there's an update to the policy.
  void OnPolicyUpdated();

  // Utility method that resets the relevant members that hold the state of
  // logged in users/sessions when the state changes, for example when users log
  // out.
  void ResetPrimaryUser();

  // Create directories in the daemon-store where the input and output files
  // will live.
  bool CreateUserDirectories() const;

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success) const;

  // Returns true if the primary user is in the allowlist of domains or users
  // who are allowed to include firmware dumps in feedback reports.
  bool PrimaryUserInAllowlist() const;

  // Proxy for dbus communication with session manager / login.
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;

  // Username of the primary user. Empty if no primary user present.
  std::string primary_user_;

  // Sanitized username of the primary user. Daemon store folders are under
  // /run/daemon-store/fbpreprocessord/${primary_user_hash_}.
  // Empty if no primary user present.
  std::string primary_user_hash_;

  // Number of concurrently active sessions. Includes incognito sessions. If
  // more than 1 session is active, do not allow adding firmware dumps to
  // feedback reports.
  int active_sessions_num_;

  // When the user logs in, we retrieve the policy and use this field to track
  // if the UserFeedbackWithLowLevelDebugDataAllowed policy allows us to
  // process firmware dumps.
  bool fw_dumps_allowed_by_policy_;

  // List of SessionStateManager observers
  base::ObserverList<Observer>::Unchecked observers_;

  // Pointer to the Manager class that instantiates all the main modules.
  Manager* manager_;

  base::WeakPtrFactory<SessionStateManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_SESSION_STATE_MANAGER_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_SESSION_STATE_MANAGER_H_
#define FBPREPROCESSOR_SESSION_STATE_MANAGER_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <debugd/dbus-proxies.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <session_manager/dbus-proxies.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/platform_features_client.h"

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

class SessionStateManager : public SessionStateManagerInterface,
                            public PlatformFeaturesClientInterface::Observer {
 public:
  explicit SessionStateManager(
      Manager* manager,
      org::chromium::SessionManagerInterfaceProxyInterface*
          session_manager_proxy,
      org::chromium::debugdProxyInterface* debugd_proxy);
  SessionStateManager(const SessionStateManager&) = delete;
  SessionStateManager& operator=(const SessionStateManager&) = delete;
  ~SessionStateManager() override;

  void AddObserver(SessionStateManagerInterface::Observer* observer) override;
  void RemoveObserver(
      SessionStateManagerInterface::Observer* observer) override;
  bool RefreshPrimaryUser();

  // Called by |PlatformFeaturesClientInterface| after fetching Finch.
  void OnFeatureChanged(bool allowed) override;

  // Returns true if the user is allowed to include firmware dumps in feedback
  // reports, false otherwise.
  bool FirmwareDumpsAllowedByPolicy(FirmwareDump::Type type) const;

  void set_base_dir_for_test(const base::FilePath& base_dir) {
    base_dir_ = base_dir;
  }

 private:
  void OnSessionStateChanged(const std::string& state);

  // Performs tasks for user login, including clearing debug buffer and then
  // notifying all observers the user login event. If the debug buffer clearing
  // task is successful, fetch the policy for the current user session.
  void HandleUserLogin();

  // Performs tasks for user logout, including clearing debug buffer and
  // notifying all observers the user logout event. Unlike |HandleUserLogin|,
  // the debug buffer clearing task is best-effort and other tasks do not
  // depend on it.
  void HandleUserLogout();

  // Handler for the response of the asynchronous D-Bus method call to
  // |org.chromium.debugd.ClearFirmwareDumpBuffer| that clears debug buffer.
  // |is_login| indicates if this function is invoked during user login.
  // |success| is the D-Bus method return indicating if the low-level execution
  // is successful.
  void OnClearFirmwareDumpBufferResponse(bool is_login, bool success);

  // Handler for the error return of the asynchronous D-Bus method call to
  // |org.chromium.debugd.ClearFirmwareDumpBuffer| that clears debug buffer.
  void OnClearFirmwareDumpBufferError(brillo::Error* error);

  // Notify all observers the user login and logout events
  void NotifyObserversOnUserLogin();
  void NotifyObserversOnUserLogout();

  void EmitFeatureAllowedMetric();

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

  // Fetch the policy from login_manager and see if
  // |UserFeedbackWithLowLevelDebugDataAllowed| is set to allow firmware dumps.
  // Returns true if fetching and parsing the policy was successful.
  bool RetrieveAndParsePolicy(
      org::chromium::SessionManagerInterfaceProxyInterface* proxy,
      const login_manager::PolicyDescriptor& descriptor);

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
  org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy_;

  // Proxy for dbus communication with debugd.
  org::chromium::debugdProxyInterface* debugd_proxy_;

  // Base directory to the root of the daemon-store where the firmware dumps are
  // stored, typically /run/daemon-store/fbpreprocessord/. Unit tests can
  // replace this directory with local temporary directories.
  base::FilePath base_dir_;

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
  bool wifi_fw_dumps_allowed_by_policy_;
  bool bluetooth_fw_dumps_allowed_by_policy_;

  // When the user logs in, it takes a little time to fetch the policy. This
  // boolean is flipped to true once we've retrieved all the necessary policy
  // information.
  bool fw_dumps_policy_loaded_;

  // Set to true if we have already retrieved the Finch setting.
  bool finch_loaded_;

  // List of SessionStateManager observers
  base::ObserverList<SessionStateManagerInterface::Observer>::Unchecked
      observers_;

  // Pointer to the Manager class that instantiates all the main modules.
  Manager* manager_;

  base::WeakPtrFactory<SessionStateManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_SESSION_STATE_MANAGER_H_

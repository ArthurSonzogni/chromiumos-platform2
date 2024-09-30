// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_SESSION_STATE_MANAGER_SESSION_STATE_MANAGER_H_
#define ODML_SESSION_STATE_MANAGER_SESSION_STATE_MANAGER_H_

#include <memory>
#include <string>
#include <utility>

#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <session_manager/dbus-proxies.h>

namespace odml {

class SessionStateManagerInterface {
 public:
  struct User {
    // The user name in clear text.
    std::string name;
    // The sanitized user name in hash format.
    std::string hash;

    bool operator==(const User&) const = default;
  };

  // Interface for observing session state changes. Objects that want to be
  // notified when the user logs in/out can add themselves to the list of
  // observers.
  class Observer {
   public:
    virtual ~Observer() = default;

    // Called when the primary user was logged in.
    // |user| is the primary user.
    virtual void OnUserLoggedIn(const User& user) = 0;

    // Called when the users were logged out (CrOS logout all users together)
    virtual void OnUserLoggedOut() = 0;
  };

  virtual ~SessionStateManagerInterface() = default;

  // Adds and removes the observer.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

class SessionStateManager : public SessionStateManagerInterface {
 public:
  explicit SessionStateManager(dbus::Bus* bus);
  explicit SessionStateManager(
      std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
          session_manager_proxy);

  SessionStateManager(const SessionStateManager&) = delete;
  SessionStateManager& operator=(const SessionStateManager&) = delete;
  ~SessionStateManager() override = default;

  void AddObserver(SessionStateManagerInterface::Observer* observer) override;
  void RemoveObserver(
      SessionStateManagerInterface::Observer* observer) override;

  // Refreshes primary user and trigger OnUserLoggedIn() or
  // OnUserLoggedOut() events when needed.
  bool RefreshPrimaryUser();

 private:
  // Callback when session state changes.
  void OnSessionStateChanged(const std::string& state);

  // Performs tasks for user login.
  void HandlUserLogin(const User& user);

  // Performs tasks for user logout.
  void HandlUserLogout();

  // Query session manager for the current primary user.
  // If successful, returns the user name and the sanitized user name. Otherwise
  // returns std::nullopt.
  std::optional<User> RetrievePrimaryUser();

  // Updates primary user internally.
  // Returns whether the primary user is retrieved and updated successfully.
  bool UpdatePrimaryUser();

  // Needed as the session manager proxy callback.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success) const;

  // Proxy for dbus communication with session manager.
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;

  // User name and sanitized user name of the primary user.
  // If no user is logged in, it is std::nullopt.
  std::optional<User> primary_user_;

  // List of SessionStateManager observers
  base::ObserverList<SessionStateManagerInterface::Observer>::Unchecked
      observers_;

  // Always keep it as the last member.
  base::WeakPtrFactory<SessionStateManager> weak_factory_{this};
};

}  // namespace odml

#endif  // ODML_SESSION_STATE_MANAGER_SESSION_STATE_MANAGER_H_

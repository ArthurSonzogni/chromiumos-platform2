// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_SESSION_STATE_MANAGER_H_
#define FBPREPROCESSOR_SESSION_STATE_MANAGER_H_

#include <string>
#include <utility>

#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <dbus/object_proxy.h>

namespace fbpreprocessor {

namespace dbus_constants {
inline constexpr char kSessionStateStarted[] = "started";
inline constexpr char kSessionStateStopped[] = "stopped";
inline constexpr char kDBusErrorNoReply[] =
    "org.freedesktop.DBus.Error.NoReply";
inline constexpr char kDBusErrorServiceUnknown[] =
    "org.freedesktop.DBus.Error.ServiceUnknown";
}  // namespace dbus_constants

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
  explicit SessionStateManager(dbus::Bus* bus);
  SessionStateManager(const SessionStateManager&) = delete;
  SessionStateManager& operator=(const SessionStateManager&) = delete;
  ~SessionStateManager() override = default;

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  void OnSessionStateChanged(dbus::Signal* signal);

  // Query session manager for the current primary user. Returns std::nullopt
  // when there was an error while getting primary user.
  // If successful, the first member of the pair is the username and the second
  // member is the sanitized username which is used for the home directory.
  std::optional<std::pair<std::string, std::string>> RetrievePrimaryUser();

  // Updates primary user internally.
  bool UpdatePrimaryUser();

  bool RefreshPrimaryUser();

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  // Proxy for dbus communication with session manager / login.
  scoped_refptr<dbus::ObjectProxy> session_manager_proxy_;

  // Username of the primary user. Empty if no primary user present.
  std::string primary_user_;

  // Sanitized username of the primary user. Daemon store folders are under
  // /run/daemon-store/fbpreprocessord/${primary_user_hash_}.
  // Empty if no primary user present.
  std::string primary_user_hash_;

  // List of SessionStateManager observers
  base::ObserverList<Observer>::Unchecked observers_;

  base::WeakPtrFactory<SessionStateManager> weak_factory{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_SESSION_STATE_MANAGER_H_

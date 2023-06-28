// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_SESSION_MANAGER_CLIENT_H_
#define BOOTSPLASH_SESSION_MANAGER_CLIENT_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/observer_list.h>
#include <session_manager-client/session_manager/dbus-proxies.h>

#include "bootsplash/session_event_observer.h"
#include "bootsplash/session_manager_client_interface.h"

namespace bootsplash {

// Connects to the system D-Bus and listens for signals from the session
// manager.
class SessionManagerClient : public SessionManagerClientInterface {
 public:
  static std::unique_ptr<SessionManagerClientInterface> Create(
      const scoped_refptr<dbus::Bus>& bus);
  ~SessionManagerClient() override = default;

  SessionManagerClient(const SessionManagerClient&) = delete;
  SessionManagerClient& operator=(const SessionManagerClient&) = delete;

  // SessionManagerClientInterface Implementation.
  void AddObserver(SessionEventObserver* observer) override;
  bool HasObserver(SessionEventObserver* observer) override;
  void RemoveObserver(SessionEventObserver* observer) override;

 private:
  // Constructs a SessionManager dbus client with signals dispatched to
  // |observers|.
  explicit SessionManagerClient(const scoped_refptr<dbus::Bus>& bus);

  // LoginPromptVisible handler.
  void LoginPromptVisible();

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  std::unique_ptr<org::chromium::SessionManagerInterfaceProxy> proxy_;
  base::ObserverList<SessionEventObserver> observers_;
  base::WeakPtrFactory<SessionManagerClient> weak_factory_{this};
};

}  // namespace bootsplash

#endif  // BOOTSPLASH_SESSION_MANAGER_CLIENT_H_

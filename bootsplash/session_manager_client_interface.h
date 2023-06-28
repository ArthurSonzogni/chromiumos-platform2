// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_SESSION_MANAGER_CLIENT_INTERFACE_H_
#define BOOTSPLASH_SESSION_MANAGER_CLIENT_INTERFACE_H_

#include <string>

#include <session_manager-client/session_manager/dbus-proxies.h>

#include "bootsplash/session_event_observer.h"

namespace bootsplash {

class SessionManagerClientInterface {
 public:
  SessionManagerClientInterface() = default;
  SessionManagerClientInterface(const SessionManagerClientInterface&) = delete;
  SessionManagerClientInterface& operator=(
      const SessionManagerClientInterface&) = delete;

  virtual ~SessionManagerClientInterface() = default;

  // Interface to add observers interested in session_manager events.
  virtual void AddObserver(SessionEventObserver* observer) = 0;
  virtual bool HasObserver(SessionEventObserver* observer) = 0;
  virtual void RemoveObserver(SessionEventObserver* observer) = 0;
};

}  // namespace bootsplash

#endif  // BOOTSPLASH_SESSION_MANAGER_CLIENT_INTERFACE_H_

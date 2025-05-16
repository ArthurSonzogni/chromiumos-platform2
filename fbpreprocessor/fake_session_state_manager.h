// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FAKE_SESSION_STATE_MANAGER_H_
#define FBPREPROCESSOR_FAKE_SESSION_STATE_MANAGER_H_

#include <base/observer_list.h>

#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/platform_features_client.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

// This class simulates the behavior of the daemon |SessionStateManager| object
// without system dependencies like D-Bus. That makes it easier to write unit
// tests.
class FakeSessionStateManager
    : public SessionStateManagerInterface,
      public PlatformFeaturesClientInterface::Observer {
 public:
  FakeSessionStateManager();
  ~FakeSessionStateManager() override = default;

  void AddObserver(SessionStateManagerInterface::Observer* observer) override;
  void RemoveObserver(
      SessionStateManagerInterface::Observer* observer) override;
  void OnFeatureChanged(bool allowed) override {};

  // The "real" daemon receives D-Bus signals when the user logs in. Since we
  // don't have D-Bus in unit tests, call this function instead to simulate what
  // happens when the user logs in.
  void SimulateLogin();

  // The "real" daemon receives D-Bus signals when the user logs out. Since we
  // don't have D-Bus in unit tests, call this function instead to simulate what
  // happens when the user logs out.
  void SimulateLogout();

 private:
  // List of SessionStateManager observers
  base::ObserverList<SessionStateManagerInterface::Observer>::Unchecked
      observers_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FAKE_SESSION_STATE_MANAGER_H_

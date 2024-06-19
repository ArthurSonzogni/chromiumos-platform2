// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_MANAGER_H_
#define SHILL_NETWORK_MOCK_NETWORK_MANAGER_H_

#include <gmock/gmock.h>

#include "shill/control_interface.h"
#include "shill/network/network.h"
#include "shill/network/network_manager.h"

namespace shill {

class MockNetworkManager : public NetworkManager {
 public:
  explicit MockNetworkManager(ControlInterface* control_interface);
  ~MockNetworkManager() override;

  MOCK_METHOD(Network*, GetNetwork, (int), (const, override));
};

}  // namespace shill
#endif  // SHILL_NETWORK_MOCK_NETWORK_MANAGER_H_

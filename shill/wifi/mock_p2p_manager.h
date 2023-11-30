// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_P2P_MANAGER_H_
#define SHILL_WIFI_MOCK_P2P_MANAGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "shill/wifi/p2p_manager.h"

namespace shill {

class MockP2PManager : public P2PManager {
 public:
  explicit MockP2PManager(Manager* manager) : P2PManager(manager) {}
  MockP2PManager(const MockP2PManager&) = delete;
  MockP2PManager& operator=(const MockP2PManager&) = delete;

  ~MockP2PManager() override = default;

  MOCK_METHOD(SupplicantP2PDeviceProxyInterface*,
              SupplicantP2PDeviceProxy,
              (),
              (const));
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_P2P_MANAGER_H_

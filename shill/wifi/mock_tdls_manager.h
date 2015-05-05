// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_TDLS_MANAGER_H_
#define SHILL_WIFI_MOCK_TDLS_MANAGER_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/wifi/tdls_manager.h"

#include "shill/error.h"

namespace shill {

class MockTDLSManager : public TDLSManager {
 public:
  MockTDLSManager();
  ~MockTDLSManager() override;

  MOCK_METHOD3(PerformOperation,
               std::string(const std::string &peer_mac_address,
                           const std::string &operation,
                           Error *error));
  MOCK_METHOD1(OnDiscoverResponseReceived,
               void(const std::string &peer_mac_address));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockTDLSManager);
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_TDLS_MANAGER_H_

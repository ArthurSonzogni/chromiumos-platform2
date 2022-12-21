// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_SLAAC_CONTROLLER_H_
#define SHILL_NETWORK_MOCK_SLAAC_CONTROLLER_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/net/ip_address.h"
#include "shill/network/slaac_controller.h"

namespace shill {

class MockSLAACController : public SLAACController {
 public:
  MockSLAACController() : SLAACController(0, nullptr, nullptr) {}
  MockSLAACController(const MockSLAACController&) = delete;
  MockSLAACController& operator=(const MockSLAACController&) = delete;

  ~MockSLAACController() override = default;

  MOCK_METHOD(void, StartRTNL, (), (override));

  MOCK_METHOD(std::vector<IPAddress>, GetAddresses, (), (const override));

  MOCK_METHOD(std::vector<IPAddress>, GetRDNSSAddresses, (), (const override));

  void RegisterCallback(UpdateCallback update_callback) override {
    update_callback_ = update_callback;
  }

  void TriggerCallback(UpdateType update_callback) {
    update_callback_.Run(update_callback);
  }

 private:
  UpdateCallback update_callback_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_SLAAC_CONTROLLER_H_

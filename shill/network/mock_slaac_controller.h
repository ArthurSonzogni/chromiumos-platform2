// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_SLAAC_CONTROLLER_H_
#define SHILL_NETWORK_MOCK_SLAAC_CONTROLLER_H_

#include <gmock/gmock.h>

#include "shill/network/slaac_controller.h"

namespace shill {

class MockSLAACController : public SLAACController {
 public:
  MockSLAACController();
  MockSLAACController(const MockSLAACController&) = delete;
  MockSLAACController& operator=(const MockSLAACController&) = delete;

  ~MockSLAACController() override;

  MOCK_METHOD(void, Start, (std::optional<net_base::IPv6Address>), (override));
  MOCK_METHOD(void, Stop, (), (override));

  MOCK_METHOD(NetworkConfig, GetNetworkConfig, (), (const override));

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

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_LEGACY_DHCP_CONTROLLER_H_
#define SHILL_NETWORK_MOCK_LEGACY_DHCP_CONTROLLER_H_

#include <optional>
#include <string>

#include <gmock/gmock.h>

#include "shill/network/legacy_dhcp_controller.h"
#include "shill/technology.h"

namespace shill {

class MockLegacyDHCPController : public LegacyDHCPController {
 public:
  MockLegacyDHCPController(ControlInterface* control_interface,
                           const std::string& device_name);
  MockLegacyDHCPController(const MockLegacyDHCPController&) = delete;
  MockLegacyDHCPController& operator=(const MockLegacyDHCPController&) = delete;

  ~MockLegacyDHCPController() override;

  void RegisterCallbacks(UpdateCallback update_callback,
                         DropCallback drop_callback) override;
  void TriggerUpdateCallback(const net_base::NetworkConfig& network_config,
                             const DHCPv4Config::Data& dhcp_data);
  void TriggerDropCallback(bool is_voluntary);
  void ProcessEventSignal(ClientEventReason reason,
                          const KeyValueStore& configuration) override;

  MOCK_METHOD(bool, RequestIP, (), (override));
  MOCK_METHOD(bool, ReleaseIP, (ReleaseReason), (override));
  MOCK_METHOD(bool, RenewIP, (), (override));

 private:
  UpdateCallback update_callback_;
  DropCallback drop_callback_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_LEGACY_DHCP_CONTROLLER_H_

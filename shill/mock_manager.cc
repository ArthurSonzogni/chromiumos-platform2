// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_manager.h"

#include <memory>

#include <gmock/gmock.h>

#include "shill/ethernet/mock_ethernet_provider.h"
#include "shill/wifi/wifi_provider.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace shill {

MockManager::MockManager(ControlInterface* control_interface,
                         EventDispatcher* dispatcher,
                         Metrics* metrics)
    : Manager(control_interface, dispatcher, metrics, "", "", ""),
      mock_ethernet_provider_(new MockEthernetProvider()) {
  mock_device_info_ = std::make_unique<NiceMock<MockDeviceInfo>>(this);
  mock_cellular_service_provider_ =
      std::make_unique<NiceMock<MockCellularServiceProvider>>(this);

  ON_CALL(*this, ethernet_provider())
      .WillByDefault(Return(mock_ethernet_provider_.get()));
  ON_CALL(*this, device_info()).WillByDefault(Return(mock_device_info_.get()));
  ON_CALL(*this, cellular_service_provider())
      .WillByDefault(Return(mock_cellular_service_provider_.get()));
  ON_CALL(*this, GetCACertExperimentPhase())
      .WillByDefault(Return(EapCredentials::CaCertExperimentPhase::kDisabled));
}

MockManager::MockManager(ControlInterface* control_interface,
                         EventDispatcher* dispatcher,
                         Metrics* metrics,
                         const std::string& run_directory,
                         const std::string& storage_directory,
                         const std::string& user_storage_directory)
    : Manager(control_interface,
              dispatcher,
              metrics,
              run_directory,
              storage_directory,
              user_storage_directory) {}

MockManager::~MockManager() = default;

void MockManager::set_wifi_provider(std::unique_ptr<WiFiProvider> provider) {
  this->wifi_provider_ = std::move(provider);
  this->UpdateProviderMapping();
}

}  // namespace shill

// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_MANAGER_H_
#define SHILL_MOCK_MANAGER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "shill/cellular/mock_cellular_service_provider.h"
#include "shill/manager.h"
#include "shill/mock_device_info.h"
#include "shill/network/dhcp_controller.h"

namespace shill {

class MockEthernetProvider;

class MockManager : public Manager {
 public:
  MockManager(ControlInterface* control_interface,
              EventDispatcher* dispatcher,
              Metrics* metrics);
  MockManager(ControlInterface* control_interface,
              EventDispatcher* dispatcher,
              Metrics* metrics,
              const std::string& run_directory,
              const std::string& storage_directory,
              const std::string& user_storage_directory);
  MockManager(const MockManager&) = delete;
  MockManager& operator=(const MockManager&) = delete;

  ~MockManager() override;

  MOCK_METHOD(DeviceInfo*, device_info, (), (override));
  MOCK_METHOD(ModemInfo*, modem_info, (), (override));
  MOCK_METHOD(CellularServiceProvider*,
              cellular_service_provider,
              (),
              (override));
  MOCK_METHOD(EthernetProvider*, ethernet_provider, (), (override));
  MOCK_METHOD(EthernetEapProvider*,
              ethernet_eap_provider,
              (),
              (const, override));
  MOCK_METHOD(const PropertyStore&, store, (), (const, override));
  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void,
              SetProfileForService,
              (const ServiceRefPtr&, const std::string&, Error*),
              (override));
  MOCK_METHOD(bool,
              MatchProfileWithService,
              (const ServiceRefPtr&),
              (override));
  MOCK_METHOD(bool,
              MoveServiceToProfile,
              (const ServiceRefPtr&, const ProfileRefPtr& destination),
              (override));
  MOCK_METHOD(void, RegisterDevice, (const DeviceRefPtr&), (override));
  MOCK_METHOD(void, DeregisterDevice, (const DeviceRefPtr&), (override));
  MOCK_METHOD(bool, HasService, (const ServiceRefPtr&), (override));
  MOCK_METHOD(void, RegisterService, (const ServiceRefPtr&), (override));
  MOCK_METHOD(void, UpdateService, (const ServiceRefPtr&), (override));
  MOCK_METHOD(void, DeregisterService, (const ServiceRefPtr&), (override));
  MOCK_METHOD(void, UpdateDevice, (const DeviceRefPtr&), (override));
  MOCK_METHOD(void,
              OnDeviceGeolocationInfoUpdated,
              (const DeviceRefPtr&),
              (override));
  MOCK_METHOD(void, RemoveService, (const ServiceRefPtr&), (override));
  MOCK_METHOD(bool,
              HandleProfileEntryDeletion,
              (const ProfileRefPtr&, const std::string&),
              (override));
  MOCK_METHOD(ServiceRefPtr,
              GetServiceWithStorageIdentifierFromProfile,
              (const ProfileRefPtr&, const std::string&, Error*),
              (override));
  MOCK_METHOD(ServiceRefPtr,
              CreateTemporaryServiceFromProfile,
              (const ProfileRefPtr&, const std::string&, Error*),
              (override));
  MOCK_METHOD(bool, IsConnected, (), (const, override));
  MOCK_METHOD(bool, IsOnline, (), (const, override));
  MOCK_METHOD(void, UpdateEnabledTechnologies, (), (override));
  MOCK_METHOD(bool, IsPortalDetectionEnabled, (Technology), (override));
  MOCK_METHOD(bool,
              IsServiceEphemeral,
              (const ServiceConstRefPtr&),
              (const, override));
  MOCK_METHOD(bool,
              IsProfileBefore,
              (const ProfileRefPtr&, const ProfileRefPtr&),
              (const, override));
  MOCK_METHOD(bool, IsTechnologyConnected, (Technology), (const, override));
  MOCK_METHOD(bool,
              IsTechnologyAutoConnectDisabled,
              (Technology),
              (const, override));
  MOCK_METHOD(void, RequestScan, (const std::string&, Error*), (override));
  MOCK_METHOD(bool, IsSuspending, (), (override));
  MOCK_METHOD(DeviceRefPtr,
              GetEnabledDeviceWithTechnology,
              (Technology),
              (const, override));
  MOCK_METHOD(ServiceRefPtr, GetFirstEthernetService, (), (override));
  MOCK_METHOD(DeviceRefPtr,
              FindDeviceFromService,
              (const ServiceRefPtr&),
              (const, override));
  MOCK_METHOD(void, ConnectToBestWiFiService, (), (override));
  MOCK_METHOD(void,
              GenerateFirmwareDumpForTechnology,
              (Technology),
              (override));
  MOCK_METHOD(const ManagerProperties&, GetProperties, (), (const, override));
  MOCK_METHOD(std::vector<DeviceRefPtr>,
              FilterByTechnology,
              (Technology tech),
              (const, override));
  MOCK_METHOD(void, RefreshTetheringCapabilities, (), ());
  MOCK_METHOD(void, TetheringStatusChanged, (), ());
  MOCK_METHOD(DHCPController::Options,
              CreateDefaultDHCPOption,
              (),
              (const, override));
  MOCK_METHOD(Network*,
              FindActiveNetworkFromService,
              (const ServiceRefPtr&),
              (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GetCellularOperatorCountryCode,
              (),
              ());
  MOCK_METHOD(EapCredentials::CaCertExperimentPhase,
              GetCACertExperimentPhase,
              (),
              (override));

  int64_t GetSuspendDurationUsecs() const override { return 1000000; }

  // Getter and setter for a mocked device info instance.
  MockDeviceInfo* mock_device_info() { return mock_device_info_.get(); }
  void set_mock_device_info(std::unique_ptr<MockDeviceInfo> mock_device_info) {
    mock_device_info_ = std::move(mock_device_info);
  }

  void set_wifi_provider(std::unique_ptr<WiFiProvider> provider);

 private:
  std::unique_ptr<MockDeviceInfo> mock_device_info_;
  std::unique_ptr<MockEthernetProvider> mock_ethernet_provider_;
  std::unique_ptr<MockCellularServiceProvider> mock_cellular_service_provider_;
};

}  // namespace shill

#endif  // SHILL_MOCK_MANAGER_H_

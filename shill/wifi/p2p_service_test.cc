// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_service.h"

#include <memory>
#include <string>
#include <vector>

#include <base/test/mock_callback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_local_device.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::StrictMock;

namespace shill {

namespace {
constexpr char kHexSSID[] = "74657374";  // Hex encode for "test"
constexpr char kPassphrase[] = "passphrase";
constexpr uint32_t kFrequency = 2437;
}  // namespace

class P2PServiceTest : public testing::Test {
 public:
  P2PServiceTest() : manager_(&control_interface_, &dispatcher_, &metrics_) {}
  ~P2PServiceTest() override = default;

  LocalDeviceConstRefPtr CreateP2PDevice(LocalDevice::IfaceType type) {
    scoped_refptr<MockLocalDevice> device = new NiceMock<MockLocalDevice>(
        &manager_, type, "p2p-wlan0-0", 0, cb.Get());
    return device;
  }

  std::unique_ptr<P2PService> CreateP2PService(
      LocalDeviceConstRefPtr device,
      std::optional<std::string> ssid,
      std::optional<std::string> passphrase,
      std::optional<uint32_t> frequency) {
    std::unique_ptr<P2PService> service =
        std::make_unique<P2PService>(device, ssid, passphrase, frequency);
    return service;
  }

 private:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      cb;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
};

TEST_F(P2PServiceTest, GetGOConfig) {
  auto device = CreateP2PDevice(LocalDevice::IfaceType::kP2PGO);
  auto service = CreateP2PService(device, kHexSSID, kPassphrase, kFrequency);

  KeyValueStore params = service->GetSupplicantConfigurationParameters();
  EXPECT_FALSE(params.IsEmpty());

  EXPECT_TRUE(
      params.Contains<Integer>(WPASupplicant::kGroupAddPropertyFrequency));
  EXPECT_EQ(params.Get<Integer>(WPASupplicant::kGroupAddPropertyFrequency),
            kFrequency);

  EXPECT_TRUE(
      params.Contains<Boolean>(WPASupplicant::kGroupAddPropertyPersistent));
  EXPECT_FALSE(params.Get<Boolean>(WPASupplicant::kGroupAddPropertyPersistent));
}

TEST_F(P2PServiceTest, GetGOConfig_NoProperties) {
  auto device = CreateP2PDevice(LocalDevice::IfaceType::kP2PGO);
  auto service =
      CreateP2PService(device, std::nullopt, std::nullopt, std::nullopt);

  KeyValueStore params = service->GetSupplicantConfigurationParameters();
  EXPECT_FALSE(params.IsEmpty());

  EXPECT_FALSE(
      params.Contains<Integer>(WPASupplicant::kGroupAddPropertyFrequency));

  EXPECT_TRUE(
      params.Contains<Boolean>(WPASupplicant::kGroupAddPropertyPersistent));
  EXPECT_FALSE(params.Get<Boolean>(WPASupplicant::kGroupAddPropertyPersistent));
}

TEST_F(P2PServiceTest, GetClientConfig) {
  auto device = CreateP2PDevice(LocalDevice::IfaceType::kP2PClient);
  auto service = CreateP2PService(device, kHexSSID, kPassphrase, kFrequency);
  KeyValueStore params = service->GetSupplicantConfigurationParameters();
  EXPECT_FALSE(params.IsEmpty());

  EXPECT_TRUE(
      params.Contains<String>(WPASupplicant::kAddPersistentGroupPropertySSID));
  EXPECT_EQ(params.Get<String>(WPASupplicant::kAddPersistentGroupPropertySSID),
            kHexSSID);

  EXPECT_TRUE(params.Contains<String>(
      WPASupplicant::kAddPersistentGroupPropertyPassphrase));
  EXPECT_EQ(
      params.Get<String>(WPASupplicant::kAddPersistentGroupPropertyPassphrase),
      kPassphrase);

  EXPECT_TRUE(params.Contains<Integer>(
      WPASupplicant::kAddPersistentGroupPropertyFrequency));
  EXPECT_EQ(
      params.Get<Integer>(WPASupplicant::kAddPersistentGroupPropertyFrequency),
      kFrequency);

  EXPECT_TRUE(
      params.Contains<Integer>(WPASupplicant::kAddPersistentGroupPropertyMode));
  EXPECT_EQ(params.Get<Integer>(WPASupplicant::kAddPersistentGroupPropertyMode),
            WPASupplicant::kAddPersistentGroupModeClient);
}

TEST_F(P2PServiceTest, GetClientConfig_NoProperties) {
  auto device = CreateP2PDevice(LocalDevice::IfaceType::kP2PClient);
  auto service =
      CreateP2PService(device, std::nullopt, std::nullopt, std::nullopt);
  KeyValueStore params = service->GetSupplicantConfigurationParameters();
  EXPECT_FALSE(params.IsEmpty());

  EXPECT_FALSE(
      params.Contains<String>(WPASupplicant::kAddPersistentGroupPropertySSID));

  EXPECT_FALSE(params.Contains<String>(
      WPASupplicant::kAddPersistentGroupPropertyPassphrase));

  EXPECT_FALSE(params.Contains<Integer>(
      WPASupplicant::kAddPersistentGroupPropertyFrequency));

  EXPECT_TRUE(
      params.Contains<Integer>(WPASupplicant::kAddPersistentGroupPropertyMode));
  EXPECT_EQ(params.Get<Integer>(WPASupplicant::kAddPersistentGroupPropertyMode),
            WPASupplicant::kAddPersistentGroupModeClient);
}

TEST_F(P2PServiceTest, GetEmptyConfig) {
  auto device = CreateP2PDevice(LocalDevice::IfaceType::kUnknown);
  auto service = CreateP2PService(device, kHexSSID, kPassphrase, kFrequency);
  KeyValueStore params = service->GetSupplicantConfigurationParameters();
  EXPECT_TRUE(params.IsEmpty());
}

}  // namespace shill

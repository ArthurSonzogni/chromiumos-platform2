// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service_provider.h"

#include <set>

#include <gtest/gtest.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/fake_store.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/test_event_dispatcher.h"

using testing::NiceMock;
using testing::Return;

namespace shill {

namespace {
const char kTestDeviceName[] = "usb0";
const char kTestDeviceAddress[] = "000102030405";
const int kTestInterfaceIndex = 1;
const char kDBusService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kDBusPath("/org/freedesktop/ModemManager1/Modem/0");
// EID must be 32 chars
const char kEid1[] = "eid1_678901234567890123456789012";
const char kEid2[] = "eid2_678901234567890123456789012";
}  // namespace

class CellularServiceProviderTest : public testing::Test {
 public:
  CellularServiceProviderTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        modem_info_(&control_, &manager_),
        device_info_(&manager_),
        profile_(new NiceMock<MockProfile>(&manager_)),
        provider_(&manager_) {}

  ~CellularServiceProviderTest() override = default;

  void SetUp() override {
    provider_.Start();
    provider_.set_profile_for_testing(profile_);
    EXPECT_CALL(*profile_, GetConstStorage()).WillRepeatedly(Return(&storage_));
    EXPECT_CALL(*profile_, GetStorage()).WillRepeatedly(Return(&storage_));
    EXPECT_CALL(manager_, cellular_service_provider())
        .WillRepeatedly(Return(&provider_));
  }

  void TearDown() override { provider_.Stop(); }

  // TODO(b/154014577): Provide eID for identifying sim cards once supported.
  CellularRefPtr CreateDevice(const std::string& imsi,
                              const std::string& iccid) {
    CellularRefPtr cellular = new Cellular(
        &modem_info_, kTestDeviceName, kTestDeviceAddress, kTestInterfaceIndex,
        Cellular::kType3gpp, kDBusService, kDBusPath);
    cellular->CreateCapability(&modem_info_);
    if (!iccid.empty()) {
      Cellular::SimProperties sim_properties;
      sim_properties.iccid = iccid;
      sim_properties.imsi = imsi;
      cellular->SetPrimarySimProperties(sim_properties);
    }
    return cellular;
  }

  CellularRefPtr CreateDeviceWithEid(const std::string& imsi,
                                     const std::string& iccid,
                                     const std::string& eid) {
    CellularRefPtr cellular = CreateDevice(imsi, iccid);
    cellular->set_eid_for_testing(eid);
    return cellular;
  }

  // TODO(b/154014577): Provide eID once supported.
  void SetupCellularStore(const std::string& identifier,
                          const std::string& imsi,
                          const std::string& iccid,
                          const std::string& sim_card_id) {
    storage_.SetString(identifier, kTypeProperty, kTypeCellular);
    storage_.SetString(identifier, CellularService::kStorageImsi, imsi);
    storage_.SetString(identifier, CellularService::kStorageIccid, iccid);
    storage_.SetString(identifier, CellularService::kStorageSimCardId,
                       sim_card_id);
  }

  void StoreCellularProperty(const std::string& identifier,
                             const std::string& key,
                             const std::string& value) {
    storage_.SetString(identifier, key, value);
  }

  std::set<std::string> GetStorageGroups() { return storage_.GetGroups(); }

  const std::vector<CellularServiceRefPtr>& GetProviderServices() const {
    return provider_.services_;
  }

  CellularServiceProvider* provider() { return &provider_; }

 private:
  EventDispatcherForTest dispatcher_;
  NiceMock<MockControl> control_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  MockModemInfo modem_info_;
  NiceMock<MockDeviceInfo> device_info_;
  FakeStore storage_;
  scoped_refptr<NiceMock<MockProfile>> profile_;
  CellularServiceProvider provider_;
};

TEST_F(CellularServiceProviderTest, LoadService) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("", service->eid());
  EXPECT_TRUE(service->IsVisible());
  EXPECT_TRUE(service->connectable());

  // Stopping should remove all services.
  provider()->Stop();
  EXPECT_EQ(0u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, RemoveServices) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ(1u, GetProviderServices().size());

  provider()->RemoveServices();
  EXPECT_EQ(0u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, LoadServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  // Add an entry in the storage with a saved property (ppp_username).
  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user1");

  // Ensure that the service is loaded from storage.
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("user1", service->ppp_username());
}

TEST_F(CellularServiceProviderTest, LoadMultipleServicesFromProfile) {
  // Set up two cellular services with the same SIM Card Id.
  SetupCellularStore("cellular_1a", "imsi1a", "iccid1a", kEid1);
  SetupCellularStore("cellular_1b", "imsi1b", "iccid1b", kEid1);
  // Set up a third cellular service with a different SIM Card Id.
  SetupCellularStore("cellular_2", "imsi2", "iccid2", kEid2);

  CellularRefPtr device = CreateDeviceWithEid("imsi1a", "iccid1a", kEid1);

  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  // cellular_1a should be returned.
  EXPECT_EQ("imsi1a", service->imsi());
  EXPECT_EQ("iccid1a", service->iccid());

  // Both cellular_1a and cellular_1b services should be created.
  const std::vector<CellularServiceRefPtr>& provider_services =
      GetProviderServices();
  ASSERT_EQ(2u, provider_services.size());
  CellularServiceRefPtr service1a = provider_services[0];
  EXPECT_EQ("iccid1a", service1a->iccid());
  EXPECT_TRUE(service1a->connectable());
  CellularServiceRefPtr service1b = provider_services[1];
  EXPECT_EQ("iccid1b", service1b->iccid());
  EXPECT_FALSE(service1b->connectable());
}

// When a SIM is switched (e.g. after a hotswap), LoadServicesForDevice will be
// called with a different primary ICCID. This should create a new Service, and
// destroy the old Service when RemoveNonDeviceServices is called.
TEST_F(CellularServiceProviderTest, SwitchDeviceIccid) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ(1u, GetProviderServices().size());
  unsigned int serial_number1 = service->serial_number();

  // Adding a device with a new ICCID should create a new service with a
  // different serial number.
  Cellular::SimProperties sim_properties;
  sim_properties.iccid = "iccid2";
  sim_properties.imsi = "imsi2";
  std::vector<Cellular::SimProperties> slot_properties;
  slot_properties.push_back(sim_properties);
  device->SetSimProperties(slot_properties, 0u);
  service = provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("iccid2", service->iccid());
  provider()->RemoveNonDeviceServices(device.get());
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_NE(serial_number1, service->serial_number());

  // Stopping should remove all services.
  provider()->Stop();
  EXPECT_EQ(0u, GetProviderServices().size());
}

// When the active SIM slot is switched, UpdateServices() should update
// the State and Strength properties of the inactive Service.
TEST_F(CellularServiceProviderTest, SwitchSimSlot) {
  CellularRefPtr cellular = CreateDevice("", "");
  Cellular::SimProperties sim1_properties;
  sim1_properties.iccid = "iccid1";
  sim1_properties.imsi = "imsi1";
  Cellular::SimProperties sim2_properties;
  sim2_properties.eid = "eid";
  sim2_properties.iccid = "iccid2";
  sim2_properties.imsi = "imsi2";
  std::vector<Cellular::SimProperties> slot_properties;
  slot_properties.push_back(sim1_properties);
  slot_properties.push_back(sim2_properties);
  cellular->SetSimProperties(slot_properties, /*primary=*/0);
  CellularServiceRefPtr service1 =
      provider()->LoadServicesForDevice(cellular.get());
  ASSERT_TRUE(service1);
  EXPECT_EQ("iccid1", service1->iccid());

  // Set the Service to connected with a non 0 signal strength.
  service1->SetConnectable(true);
  service1->SetState(Service::kStateConnected);
  service1->SetStrength(50);

  // Setting the other SIM to primary should clear the |service1| properties
  // associated with being connected.
  cellular->SetSimProperties(slot_properties, /*primary=*/1);
  CellularServiceRefPtr service2 =
      provider()->LoadServicesForDevice(cellular.get());
  ASSERT_TRUE(service2);
  EXPECT_EQ("iccid2", service2->iccid());

  provider()->UpdateServices(cellular.get());
  // |service1| is still connectable since it is an available SIM.
  EXPECT_TRUE(service1->connectable());
  // |service1| State is set to Idle and Strength is set to 0.
  EXPECT_EQ(Service::kStateIdle, service1->state());
  EXPECT_EQ(0u, service1->strength());
}

TEST_F(CellularServiceProviderTest, RemoveObsoleteServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  // Add two entries in the storage with the same ICCID, one with an empty IMSI.
  // Set a property on both.
  SetupCellularStore(identifier, "", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user1");
  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user2");

  // Ensure that the service with a non empty imsi loaded from storage.
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  provider()->RemoveNonDeviceServices(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("user2", service->ppp_username());

  // Only one provider service should exist.
  EXPECT_EQ(1u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, OnServiceUnloaded) {
  CellularRefPtr device = CreateDeviceWithEid("imsi1", "iccid1", kEid1);
  std::string identifier = device->GetStorageIdentifier();

  SetupCellularStore(identifier, "imsi1", "iccid1", kEid1);
  SetupCellularStore(identifier, "imsi2", "iccid2", kEid1);

  provider()->LoadServicesForSecondarySim(kEid1, "iccid1", "imsi1",
                                          device.get());
  const std::vector<CellularServiceRefPtr>& services = GetProviderServices();
  EXPECT_EQ(2u, services.size());

  for (const auto& service : services) {
    if (service->iccid() == "iccid2") {
      service->Unload();
      break;
    }
  }
  EXPECT_EQ(1u, GetProviderServices().size());
}

}  // namespace shill

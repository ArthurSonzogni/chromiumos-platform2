// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/crostini_service.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/guest_type.h"
#include "patchpanel/ipc.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/net_util.h"
#include "patchpanel/routing_service.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::Invoke;
using testing::Mock;
using testing::Pair;
using testing::Pointee;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;
using testing::UnorderedElementsAre;

namespace patchpanel {
namespace {

class CrostiniServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    datapath_ = std::make_unique<MockDatapath>();
    addr_mgr_ = std::make_unique<AddressManager>();
  }

  std::unique_ptr<CrostiniService> NewService() {
    return std::make_unique<CrostiniService>(
        addr_mgr_.get(), datapath_.get(),
        base::BindRepeating(&CrostiniServiceTest::DeviceHandler,
                            base::Unretained(this)));
  }

  void DeviceHandler(const Device& device,
                     Device::ChangeEvent event,
                     GuestMessage::GuestType guest_type) {
    guest_devices_[device.host_ifname()] = event;
  }

  std::unique_ptr<AddressManager> addr_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::map<std::string, Device::ChangeEvent> guest_devices_;
};

TEST_F(CrostiniServiceTest, StartStopCrostiniVM) {
  constexpr uint64_t vm_id = 101;
  auto crostini = NewService();

  EXPECT_CALL(*datapath_, AddTAP("", _, _, "crosvm"))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddIPv4Route(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice("", "vmtap0", _,
                                             TrafficSource::kCrosVM, true, 0));

  // There should be no virtual device before the VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  ASSERT_TRUE(crostini->GetDevices().empty());

  // The virtual datapath for the Crostini VM can successfully start.
  ASSERT_TRUE(crostini->Start(vm_id, CrostiniService::VMType::kTermina,
                              /*subnet_index=*/0));
  Mock::VerifyAndClearExpectations(datapath_.get());
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting, there should be a virtual device.
  auto* device = crostini->GetDevice(vm_id);
  auto devices = crostini->GetDevices();
  ASSERT_NE(nullptr, device);
  ASSERT_FALSE(devices.empty());
  ASSERT_EQ(device, devices[0]);
  ASSERT_EQ("vmtap0", device->host_ifname());

  // The virtual datapath for the Crostini VM can successfully stop.
  crostini->Stop(vm_id);
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kRemoved, it->second);

  // After stopping the datapath setup, there should be no virtual device.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  ASSERT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, StartStopParallelVM) {
  constexpr uint64_t vm_id = 102;
  auto crostini = NewService();

  EXPECT_CALL(*datapath_, AddTAP("", _, _, "crosvm"))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddIPv4Route(_, _, _)).Times(0);
  EXPECT_CALL(
      *datapath_,
      StartRoutingDevice("", "vmtap0", _, TrafficSource::kPluginVM, true, 0));

  // There should be no virtual device before the VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  ASSERT_TRUE(crostini->GetDevices().empty());

  // The virtual datapath for the Parallel VM can successfully start.
  ASSERT_TRUE(crostini->Start(vm_id, CrostiniService::VMType::kParallel,
                              /*subnet_index=*/1));
  Mock::VerifyAndClearExpectations(datapath_.get());
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting, there should be a virtual device.
  auto* device = crostini->GetDevice(vm_id);
  auto devices = crostini->GetDevices();
  ASSERT_NE(nullptr, device);
  ASSERT_FALSE(devices.empty());
  ASSERT_EQ(device, devices[0]);
  ASSERT_EQ("vmtap0", device->host_ifname());

  // The virtual datapath for the Parallel VM can successfully stop.
  crostini->Stop(vm_id);
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kRemoved, it->second);

  // After stopping the datapath setup, there should be no virtual device.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  ASSERT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, MultipleVMs) {
  constexpr uint64_t vm_id1 = 101;
  constexpr uint64_t vm_id2 = 102;
  constexpr uint64_t vm_id3 = 103;
  auto crostini = NewService();

  EXPECT_CALL(*datapath_, AddTAP("", _, _, "crosvm"))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"));
  EXPECT_CALL(*datapath_, AddIPv4Route(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice("", "vmtap0", _,
                                             TrafficSource::kCrosVM, true, 0));
  EXPECT_CALL(
      *datapath_,
      StartRoutingDevice("", "vmtap1", _, TrafficSource::kPluginVM, true, 0));
  EXPECT_CALL(*datapath_, StartRoutingDevice("", "vmtap2", _,
                                             TrafficSource::kCrosVM, true, 0));

  // There should be no virtual device before any VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id1));
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id2));
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id3));
  ASSERT_TRUE(crostini->GetDevices().empty());

  // Start first Crostini VM.
  ASSERT_TRUE(crostini->Start(vm_id1, CrostiniService::VMType::kTermina,
                              /*subnet_index=*/0));
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting, there should be a virtual device for that VM.
  auto* device = crostini->GetDevice(vm_id1);
  ASSERT_NE(nullptr, device);
  ASSERT_EQ("vmtap0", device->host_ifname());

  // Start Parallel VM.
  ASSERT_TRUE(crostini->Start(vm_id2, CrostiniService::VMType::kParallel,
                              /*subnet_index=*/0));
  it = guest_devices_.find("vmtap1");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting that second VM, there should be another virtual device.
  device = crostini->GetDevice(vm_id2);
  ASSERT_NE(nullptr, device);
  ASSERT_EQ("vmtap1", device->host_ifname());

  // Start second Crostini VM.
  ASSERT_TRUE(crostini->Start(vm_id3, CrostiniService::VMType::kTermina,
                              /*subnet_index=*/0));
  it = guest_devices_.find("vmtap2");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting that third VM, there should be another virtual device.
  device = crostini->GetDevice(vm_id3);
  ASSERT_NE(nullptr, device);
  ASSERT_EQ("vmtap2", device->host_ifname());

  // There are three virtual devices owned by CrostiniService.
  auto devices = crostini->GetDevices();
  ASSERT_FALSE(devices.empty());
  for (const auto* dev : devices) {
    ASSERT_EQ(dev->host_ifname(), dev->phys_ifname());
    ASSERT_EQ("", dev->guest_ifname());
    if (dev->host_ifname() == "vmtap0") {
      ASSERT_EQ(GuestType::kTerminaVM, dev->type());
    } else if (dev->host_ifname() == "vmtap1") {
      ASSERT_EQ(GuestType::kPluginVM, dev->type());
    } else if (dev->host_ifname() == "vmtap2") {
      ASSERT_EQ(GuestType::kTerminaVM, dev->type());
    } else {
      FAIL() << "Unexpected guest Device " << dev->host_ifname();
    }
  }

  // Stop first Crostini VM. Its virtual device is destroyed.
  crostini->Stop(vm_id1);
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id1));
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kRemoved, it->second);
  guest_devices_.clear();

  // Stop second Crostini VM. Its virtual device is destroyed.
  crostini->Stop(vm_id3);
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id3));
  it = guest_devices_.find("vmtap2");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kRemoved, it->second);
  guest_devices_.clear();

  // Stop Plugin VM. Its virtual device is destroyed.
  crostini->Stop(vm_id2);
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id2));
  it = guest_devices_.find("vmtap1");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(Device::ChangeEvent::kRemoved, it->second);

  // There are no more virtual devices left.
  ASSERT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, VMTypeConversions) {
  EXPECT_EQ(CrostiniService::VMType::kTermina,
            CrostiniService::VMTypeFromGuestType(GuestType::kTerminaVM));
  EXPECT_EQ(CrostiniService::VMType::kParallel,
            CrostiniService::VMTypeFromGuestType(GuestType::kPluginVM));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromGuestType(GuestType::kArc0));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromGuestType(GuestType::kArcNet));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromGuestType(GuestType::kLXDContainer));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromGuestType(GuestType::kNetns));

  EXPECT_EQ(
      CrostiniService::VMType::kTermina,
      CrostiniService::VMTypeFromProtoGuestType(NetworkDevice::TERMINA_VM));
  EXPECT_EQ(
      CrostiniService::VMType::kParallel,
      CrostiniService::VMTypeFromProtoGuestType(NetworkDevice::PLUGIN_VM));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromProtoGuestType(NetworkDevice::ARC));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromProtoGuestType(NetworkDevice::ARCVM));
  EXPECT_EQ(std::nullopt,
            CrostiniService::VMTypeFromProtoGuestType(NetworkDevice::UNKNOWN));

  EXPECT_EQ(TrafficSource::kCrosVM, CrostiniService::TrafficSourceFromVMType(
                                        CrostiniService::VMType::kTermina));
  EXPECT_EQ(TrafficSource::kPluginVM, CrostiniService::TrafficSourceFromVMType(
                                          CrostiniService::VMType::kParallel));

  EXPECT_EQ(GuestMessage::TERMINA_VM,
            CrostiniService::GuestMessageTypeFromVMType(
                CrostiniService::VMType::kTermina));
  EXPECT_EQ(GuestMessage::PLUGIN_VM,
            CrostiniService::GuestMessageTypeFromVMType(
                CrostiniService::VMType::kParallel));

  EXPECT_EQ(GuestType::kTerminaVM, CrostiniService::GuestTypeFromVMType(
                                       CrostiniService::VMType::kTermina));
  EXPECT_EQ(GuestType::kPluginVM, CrostiniService::GuestTypeFromVMType(
                                      CrostiniService::VMType::kParallel));
}

}  // namespace
}  // namespace patchpanel

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
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/mock_datapath.h"
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

MATCHER_P(ShillDeviceHasInterfaceName, expected_ifname, "") {
  return arg.ifname == expected_ifname;
}

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

  void DeviceHandler(const CrostiniService::CrostiniDevice& device,
                     CrostiniService::CrostiniDeviceEvent event) {
    guest_devices_[device.tap_device_ifname()] = event;
  }

  std::unique_ptr<AddressManager> addr_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::map<std::string, CrostiniService::CrostiniDeviceEvent> guest_devices_;
};

TEST_F(CrostiniServiceTest, StartStopCrostiniVM) {
  constexpr uint64_t vm_id = 101;
  auto crostini = NewService();

  ShillClient::Device wlan0_dev;
  wlan0_dev.ifname = "wlan0";
  crostini->OnShillDefaultLogicalDeviceChanged(&wlan0_dev, nullptr);

  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddIPv4Route).WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap0", TrafficSource::kCrosVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT).Times(0);

  // There should be no virtual device before the VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  EXPECT_TRUE(crostini->GetDevices().empty());

  // The virtual datapath for the Crostini VM can successfully start.
  auto* device = crostini->Start(vm_id, CrostiniService::VMType::kTermina,
                                 /*subnet_index=*/0);
  Mock::VerifyAndClearExpectations(datapath_.get());
  ASSERT_NE(nullptr, device);
  EXPECT_NE(std::nullopt, device->lxd_ipv4_subnet());
  EXPECT_NE(std::nullopt, device->lxd_ipv4_address());
  EXPECT_EQ("vmtap0", device->tap_device_ifname());
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  EXPECT_EQ(CrostiniService::CrostiniDeviceEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting, there should be a virtual device.
  EXPECT_EQ(device, crostini->GetDevice(vm_id));
  auto devices = crostini->GetDevices();
  ASSERT_FALSE(devices.empty());
  EXPECT_EQ(device, devices[0]);

  // The virtual datapath for the Crostini VM can successfully stop.
  EXPECT_CALL(*datapath_, RemoveInterface("vmtap0"));
  EXPECT_CALL(*datapath_, StopRoutingDevice("vmtap0"));
  EXPECT_CALL(*datapath_, RemoveInboundIPv4DNAT).Times(0);
  crostini->Stop(vm_id);
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  EXPECT_EQ(CrostiniService::CrostiniDeviceEvent::kRemoved, it->second);

  // After stopping the datapath setup, there should be no virtual device.
  EXPECT_EQ(nullptr, crostini->GetDevice(vm_id));
  EXPECT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, StartStopParallelsVM) {
  constexpr uint64_t vm_id = 102;
  auto crostini = NewService();

  ShillClient::Device wlan0_dev;
  wlan0_dev.ifname = "wlan0";
  crostini->OnShillDefaultLogicalDeviceChanged(&wlan0_dev, nullptr);

  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddIPv4Route).Times(0);
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap0", TrafficSource::kParallelsVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kParallels,
                                 ShillDeviceHasInterfaceName("wlan0"),
                                 net_base::IPv4Address(100, 115, 93, 2)));

  // There should be no virtual device before the VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  EXPECT_TRUE(crostini->GetDevices().empty());

  // The virtual datapath for the Parallels VM can successfully start.
  auto* device = crostini->Start(vm_id, CrostiniService::VMType::kParallels,
                                 /*subnet_index=*/1);
  ASSERT_NE(nullptr, device);
  EXPECT_EQ("vmtap0", device->tap_device_ifname());
  EXPECT_EQ(std::nullopt, device->lxd_ipv4_subnet());
  EXPECT_EQ(std::nullopt, device->lxd_ipv4_address());
  Mock::VerifyAndClearExpectations(datapath_.get());
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  EXPECT_EQ(CrostiniService::CrostiniDeviceEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting, there should be a virtual device.
  EXPECT_EQ(device, crostini->GetDevice(vm_id));
  auto devices = crostini->GetDevices();
  ASSERT_FALSE(devices.empty());
  EXPECT_EQ(device, devices[0]);

  // The virtual datapath for the Parallels VM can successfully stop.
  EXPECT_CALL(*datapath_, RemoveInterface("vmtap0"));
  EXPECT_CALL(*datapath_, StopRoutingDevice("vmtap0"));
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kParallels,
                                    ShillDeviceHasInterfaceName("wlan0"),
                                    net_base::IPv4Address(100, 115, 93, 2)));
  crostini->Stop(vm_id);
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  EXPECT_EQ(CrostiniService::CrostiniDeviceEvent::kRemoved, it->second);

  // After stopping the datapath setup, there should be no virtual device.
  EXPECT_EQ(nullptr, crostini->GetDevice(vm_id));
  EXPECT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, StartStopBruschettaVM) {
  constexpr uint64_t vm_id = 101;
  auto crostini = NewService();

  ShillClient::Device wlan0_dev;
  wlan0_dev.ifname = "wlan0";
  crostini->OnShillDefaultLogicalDeviceChanged(&wlan0_dev, nullptr);

  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddIPv4Route).Times(0);
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap0", TrafficSource::kCrosVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT).Times(0);

  // There should be no virtual device before the VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id));
  EXPECT_TRUE(crostini->GetDevices().empty());

  // The virtual datapath for the Bruschetta VM can successfully start.
  auto* device = crostini->Start(vm_id, CrostiniService::VMType::kBruschetta,
                                 /*subnet_index=*/0);
  Mock::VerifyAndClearExpectations(datapath_.get());
  ASSERT_NE(nullptr, device);
  EXPECT_EQ(std::nullopt, device->lxd_ipv4_subnet());
  EXPECT_EQ(std::nullopt, device->lxd_ipv4_address());
  EXPECT_EQ("vmtap0", device->tap_device_ifname());
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  EXPECT_EQ(CrostiniService::CrostiniDeviceEvent::kAdded, it->second);
  guest_devices_.clear();

  // After starting, there should be a virtual device.
  EXPECT_EQ(device, crostini->GetDevice(vm_id));
  auto devices = crostini->GetDevices();
  ASSERT_FALSE(devices.empty());
  EXPECT_EQ(device, devices[0]);

  // The virtual datapath for the Bruschetta VM can successfully stop.
  EXPECT_CALL(*datapath_, RemoveInterface("vmtap0"));
  EXPECT_CALL(*datapath_, StopRoutingDevice("vmtap0"));
  EXPECT_CALL(*datapath_, RemoveInboundIPv4DNAT).Times(0);
  crostini->Stop(vm_id);
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  EXPECT_EQ(CrostiniService::CrostiniDeviceEvent::kRemoved, it->second);

  // After stopping the datapath setup, there should be no virtual device.
  EXPECT_EQ(nullptr, crostini->GetDevice(vm_id));
  EXPECT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, MultipleVMs) {
  constexpr uint64_t vm_id1 = 101;
  constexpr uint64_t vm_id2 = 102;
  constexpr uint64_t vm_id3 = 103;
  auto crostini = NewService();

  ShillClient::Device wlan0_dev;
  wlan0_dev.ifname = "wlan0";
  crostini->OnShillDefaultLogicalDeviceChanged(&wlan0_dev, nullptr);

  // There should be no virtual device before any VM starts.
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id1));
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id2));
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id3));
  EXPECT_TRUE(crostini->GetDevices().empty());

  // Start first Crostini VM.
  EXPECT_CALL(*datapath_, AddIPv4Route).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))

      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap0", TrafficSource::kCrosVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT).Times(0);
  auto* device = crostini->Start(vm_id1, CrostiniService::VMType::kTermina,
                                 /*subnet_index=*/0);
  ASSERT_NE(nullptr, device);
  ASSERT_EQ("vmtap0", device->tap_device_ifname());
  auto it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(CrostiniService::CrostiniDeviceEvent::kAdded, it->second);
  guest_devices_.clear();
  Mock::VerifyAndClearExpectations(datapath_.get());

  // After starting, there should be a virtual device for that VM.
  ASSERT_EQ(device, crostini->GetDevice(vm_id1));

  // Start Parallels VM.
  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))
      .WillOnce(Return("vmtap1"));
  EXPECT_CALL(*datapath_, AddIPv4Route).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap1", TrafficSource::kParallelsVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kParallels,
                                 ShillDeviceHasInterfaceName("wlan0"),
                                 net_base::IPv4Address(100, 115, 93, 2)));
  device = crostini->Start(vm_id2, CrostiniService::VMType::kParallels,
                           /*subnet_index=*/0);
  ASSERT_NE(nullptr, device);
  ASSERT_EQ("vmtap1", device->tap_device_ifname());
  it = guest_devices_.find("vmtap1");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(CrostiniService::CrostiniDeviceEvent::kAdded, it->second);
  guest_devices_.clear();
  Mock::VerifyAndClearExpectations(datapath_.get());

  // After starting that second VM, there should be another virtual device.
  ASSERT_EQ(device, crostini->GetDevice(vm_id2));

  // Start second Crostini VM.
  EXPECT_CALL(*datapath_, AddIPv4Route).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))
      .WillOnce(Return("vmtap2"));
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap2", TrafficSource::kCrosVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT).Times(0);
  device = crostini->Start(vm_id3, CrostiniService::VMType::kTermina,
                           /*subnet_index=*/0);
  ASSERT_NE(nullptr, device);
  ASSERT_EQ("vmtap2", device->tap_device_ifname());
  it = guest_devices_.find("vmtap2");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(CrostiniService::CrostiniDeviceEvent::kAdded, it->second);
  guest_devices_.clear();
  Mock::VerifyAndClearExpectations(datapath_.get());

  // After starting that third VM, there should be another virtual device.
  ASSERT_EQ(device, crostini->GetDevice(vm_id3));

  // There are three virtual devices owned by CrostiniService.
  auto devices = crostini->GetDevices();
  ASSERT_FALSE(devices.empty());
  for (const auto* dev : devices) {
    if (dev->tap_device_ifname() == "vmtap0") {
      ASSERT_EQ(CrostiniService::VMType::kTermina, dev->type());
    } else if (dev->tap_device_ifname() == "vmtap1") {
      ASSERT_EQ(CrostiniService::VMType::kParallels, dev->type());
    } else if (dev->tap_device_ifname() == "vmtap2") {
      ASSERT_EQ(CrostiniService::VMType::kTermina, dev->type());
    } else {
      FAIL() << "Unexpected guest Device " << dev->tap_device_ifname();
    }
  }

  // Stop first Crostini VM. Its virtual device is destroyed.
  EXPECT_CALL(*datapath_, RemoveInterface("vmtap0"));
  EXPECT_CALL(*datapath_, StopRoutingDevice("vmtap0"));
  EXPECT_CALL(*datapath_, RemoveInboundIPv4DNAT).Times(0);
  crostini->Stop(vm_id1);
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id1));
  it = guest_devices_.find("vmtap0");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(CrostiniService::CrostiniDeviceEvent::kRemoved, it->second);
  guest_devices_.clear();
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Stop second Crostini VM. Its virtual device is destroyed.
  EXPECT_CALL(*datapath_, RemoveInterface("vmtap2"));
  EXPECT_CALL(*datapath_, StopRoutingDevice("vmtap2"));
  EXPECT_CALL(*datapath_, RemoveInboundIPv4DNAT).Times(0);
  crostini->Stop(vm_id3);
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id3));
  it = guest_devices_.find("vmtap2");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(CrostiniService::CrostiniDeviceEvent::kRemoved, it->second);
  guest_devices_.clear();
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Stop Parallels VM. Its virtual device is destroyed.
  EXPECT_CALL(*datapath_, RemoveInterface("vmtap1"));
  EXPECT_CALL(*datapath_, StopRoutingDevice("vmtap1"));
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kParallels,
                                    ShillDeviceHasInterfaceName("wlan0"),
                                    net_base::IPv4Address(100, 115, 93, 2)));
  crostini->Stop(vm_id2);
  ASSERT_EQ(nullptr, crostini->GetDevice(vm_id2));
  it = guest_devices_.find("vmtap1");
  ASSERT_NE(guest_devices_.end(), it);
  ASSERT_EQ(CrostiniService::CrostiniDeviceEvent::kRemoved, it->second);

  // There are no more virtual devices left.
  ASSERT_TRUE(crostini->GetDevices().empty());
}

TEST_F(CrostiniServiceTest, DefaultLogicalDeviceChange) {
  constexpr uint64_t vm_id1 = 101;
  constexpr uint64_t vm_id2 = 102;
  const auto parallels_addr = net_base::IPv4Address(100, 115, 93, 2);
  auto crostini = NewService();

  // Start a Crostini VM and a Parallels VM.
  EXPECT_CALL(*datapath_, AddIPv4Route).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddTunTap("", _, _, "crosvm", DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"));
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap0", TrafficSource::kCrosVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_,
              StartRoutingDeviceAsUser("vmtap1", TrafficSource::kParallelsVM, _,
                                       Eq(std::nullopt), Eq(std::nullopt),
                                       Eq(std::nullopt)));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT).Times(0);
  crostini->Start(vm_id1, CrostiniService::VMType::kTermina,
                  /*subnet_index=*/0);
  crostini->Start(vm_id2, CrostiniService::VMType::kParallels,
                  /*subnet_index=*/0);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // A logical default Device is available.
  ShillClient::Device wlan0_dev;
  wlan0_dev.ifname = "wlan0";
  EXPECT_CALL(
      *datapath_,
      AddInboundIPv4DNAT(AutoDNATTarget::kParallels,
                         ShillDeviceHasInterfaceName("wlan0"), parallels_addr));
  crostini->OnShillDefaultLogicalDeviceChanged(&wlan0_dev, nullptr);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // The logical default Device changes.
  ShillClient::Device eth0_dev;
  eth0_dev.ifname = "eth0";
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kParallels,
                                    ShillDeviceHasInterfaceName("wlan0"),
                                    parallels_addr));
  EXPECT_CALL(
      *datapath_,
      AddInboundIPv4DNAT(AutoDNATTarget::kParallels,
                         ShillDeviceHasInterfaceName("eth0"), parallels_addr));
  crostini->OnShillDefaultLogicalDeviceChanged(&eth0_dev, &wlan0_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // The logical default Device is not available anymore.
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kParallels,
                                    ShillDeviceHasInterfaceName("eth0"),
                                    parallels_addr));
  crostini->OnShillDefaultLogicalDeviceChanged(nullptr, &eth0_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(CrostiniServiceTest, VMTypeConversions) {
  EXPECT_EQ(TrafficSource::kCrosVM, CrostiniService::TrafficSourceFromVMType(
                                        CrostiniService::VMType::kTermina));
  EXPECT_EQ(TrafficSource::kParallelsVM,
            CrostiniService::TrafficSourceFromVMType(
                CrostiniService::VMType::kParallels));

  EXPECT_EQ(GuestMessage::TERMINA_VM,
            CrostiniService::GuestMessageTypeFromVMType(
                CrostiniService::VMType::kTermina));
  EXPECT_EQ(GuestMessage::PARALLELS_VM,
            CrostiniService::GuestMessageTypeFromVMType(
                CrostiniService::VMType::kParallels));

  EXPECT_EQ(AddressManager::GuestType::kTerminaVM,
            CrostiniService::AddressManagingTypeFromVMType(
                CrostiniService::VMType::kTermina));
  EXPECT_EQ(AddressManager::GuestType::kParallelsVM,
            CrostiniService::AddressManagingTypeFromVMType(
                CrostiniService::VMType::kParallels));
}

TEST_F(CrostiniServiceTest, ConvertTerminaDevice) {
  const uint32_t subnet_index = 0;
  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      AddressManager::GuestType::kTerminaVM, subnet_index);
  auto lxd_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kLXDContainer);
  auto expected_host_ipv4 =
      ipv4_subnet->CIDRAtOffset(1)->address().ToInAddr().s_addr;
  auto expected_guest_ipv4 =
      ipv4_subnet->CIDRAtOffset(2)->address().ToInAddr().s_addr;
  auto expected_base_cidr = ipv4_subnet->base_cidr();
  auto termina_device = std::make_unique<CrostiniService::CrostiniDevice>(
      CrostiniService::VMType::kTermina, "vmtap0", mac_addr,
      std::move(ipv4_subnet), std::move(lxd_subnet));

  NetworkDevice proto_device;
  termina_device->ConvertToProto(&proto_device);
  EXPECT_EQ("vmtap0", proto_device.ifname());
  // Convention for Crostini Devices is to reuse the virtual interface name in
  // place of the interface name of the upstream network used by ARC Devices.
  EXPECT_EQ("vmtap0", proto_device.phys_ifname());
  EXPECT_EQ("", proto_device.guest_ifname());
  EXPECT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  EXPECT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  EXPECT_EQ(expected_base_cidr.address(),
            net_base::IPv4Address::CreateFromBytes(
                proto_device.ipv4_subnet().addr()));
  EXPECT_EQ(expected_base_cidr.address().ToInAddr().s_addr,
            proto_device.ipv4_subnet().base_addr());
  EXPECT_EQ(expected_base_cidr.prefix_length(),
            proto_device.ipv4_subnet().prefix_len());
  EXPECT_EQ(NetworkDevice::TERMINA_VM, proto_device.guest_type());
}

TEST_F(CrostiniServiceTest, ConvertParallelsDevice) {
  const uint32_t subnet_index = 1;
  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      AddressManager::GuestType::kParallelsVM, subnet_index);
  auto expected_host_ipv4 =
      ipv4_subnet->CIDRAtOffset(1)->address().ToInAddr().s_addr;
  auto expected_guest_ipv4 =
      ipv4_subnet->CIDRAtOffset(2)->address().ToInAddr().s_addr;
  auto expected_base_cidr = ipv4_subnet->base_cidr();
  auto parallels_device = std::make_unique<CrostiniService::CrostiniDevice>(
      CrostiniService::VMType::kParallels, "vmtap1", mac_addr,
      std::move(ipv4_subnet), nullptr);

  NetworkDevice proto_device;
  parallels_device->ConvertToProto(&proto_device);
  EXPECT_EQ("vmtap1", proto_device.ifname());
  // Convention for Crostini Devices is to reuse the virtual interface name in
  // place of the interface name of the upstream network used by ARC Devices.
  EXPECT_EQ("vmtap1", proto_device.phys_ifname());
  EXPECT_EQ("", proto_device.guest_ifname());
  EXPECT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  EXPECT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  EXPECT_EQ(expected_base_cidr.address(),
            net_base::IPv4Address::CreateFromBytes(
                proto_device.ipv4_subnet().addr()));
  EXPECT_EQ(expected_base_cidr.address().ToInAddr().s_addr,
            proto_device.ipv4_subnet().base_addr());
  EXPECT_EQ(expected_base_cidr.prefix_length(),
            proto_device.ipv4_subnet().prefix_len());
  EXPECT_EQ(NetworkDevice::PARALLELS_VM, proto_device.guest_type());
}

}  // namespace
}  // namespace patchpanel

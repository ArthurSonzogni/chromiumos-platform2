// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ARC_SERVICE_H_
#define PATCHPANEL_ARC_SERVICE_H_

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/device.h"
#include "patchpanel/ipc.pb.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/traffic_forwarder.h"

namespace patchpanel {

constexpr char kArcBridge[] = "arcbr0";

class ArcService {
 public:
  // All pointers are required and cannot be null, and are owned by the caller.
  ArcService(ShillClient* shill_client,
             Datapath* datapath,
             AddressManager* addr_mgr,
             TrafficForwarder* forwarder,
             GuestMessage::GuestType guest,
             Device::ChangeEventHandler device_changed_handler);
  ArcService(const ArcService&) = delete;
  ArcService& operator=(const ArcService&) = delete;

  ~ArcService();

  bool Start(uint32_t id);
  void Stop(uint32_t id);

  // Returns a list of device configurations. This method only really is useful
  // when ARCVM is running as it enables the caller to discover which
  // configurations, if any, are currently associated to TAP devices.
  std::vector<const Device::Config*> GetDeviceConfigs() const;

  // Walks the current list of devices managed by the service invoking the
  // callback for each, allowing for safe inspection/evaluation.
  void ScanDevices(base::RepeatingCallback<void(const Device&)> callback) const;

  // Callback from ShillClient, invoked whenever the device list changes.
  // |shill_devices_| will contain all devices currently connected to shill
  // (e.g. "eth0", "wlan0", etc).
  void OnDevicesChanged(const std::set<std::string>& added,
                        const std::set<std::string>& removed);

 private:
  // Returns true if the service has been started for ARC container or ARCVM.
  bool IsStarted() const;

  // Build and configure the ARC datapath for the physical device |ifname|
  // provided by Shill.
  void AddDevice(const std::string& ifname, ShillClient::Device::Type type);

  // Teardown the ARC datapath associated with the physical device |ifname| and
  // stops forwarding services.
  void RemoveDevice(const std::string& ifname, ShillClient::Device::Type type);

  // Creates device configurations for all available IPv4 subnets which will be
  // assigned to devices as they are added.
  void AllocateAddressConfigs();

  // Reserve a configuration for an interface.
  std::unique_ptr<Device::Config> AcquireConfig(ShillClient::Device::Type type);

  // Returns a configuration to the pool.
  void ReleaseConfig(ShillClient::Device::Type type,
                     std::unique_ptr<Device::Config> config);

  ShillClient* shill_client_;
  Datapath* datapath_;
  AddressManager* addr_mgr_;
  TrafficForwarder* forwarder_;
  GuestMessage::GuestType guest_;
  Device::ChangeEventHandler device_changed_handler_;

  // A set of preallocated device configurations keyed by technology type and
  // used for setting up ARCVM tap devices at VM booting time.
  std::map<ShillClient::Device::Type,
           std::deque<std::unique_ptr<Device::Config>>>
      available_configs_;
  // The list of all Device configurations. Also includes ARC management device
  // for ARCVM.
  std::vector<Device::Config*> all_configs_;
  // The ARC device configurations corresponding to the host physical devices,
  // keyed by device interface name.
  std::map<std::string, std::unique_ptr<Device>> devices_;
  // The ARC management device used for legacy adb-over-tcp support and VPN
  // forwarding.
  std::unique_ptr<Device> arc_device_;
  // The PID of the ARC container instance or the CID of ARCVM instance.
  uint32_t id_;
  // All devices currently managed by shill.
  std::map<std::string, ShillClient::Device::Type> shill_devices_;

  FRIEND_TEST(ArcServiceTest, NotStarted_AddDevice);
  FRIEND_TEST(ArcServiceTest, NotStarted_AddRemoveDevice);

  base::WeakPtrFactory<ArcService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_ARC_SERVICE_H_

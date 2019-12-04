// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_DEVICE_MANAGER_H_
#define ARC_NETWORK_DEVICE_MANAGER_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/memory/weak_ptr.h>
#include <shill/net/rtnl_listener.h>

#include "arc/network/address_manager.h"
#include "arc/network/datapath.h"
#include "arc/network/device.h"
#include "arc/network/helper_process.h"
#include "arc/network/ipc.pb.h"
#include "arc/network/shill_client.h"

namespace arc_networkd {
using DeviceHandler = base::Callback<void(Device*)>;
using NameHandler = base::Callback<void(const std::string&)>;

// Mockable base class for DeviceManager.
class DeviceManagerBase {
 public:
  DeviceManagerBase() = default;
  virtual ~DeviceManagerBase() = default;

  // Used by guest service implementations to be notified when tracked devices
  // are updated.
  virtual void RegisterDeviceAddedHandler(GuestMessage::GuestType guest,
                                          const DeviceHandler& handler) = 0;
  virtual void RegisterDeviceRemovedHandler(GuestMessage::GuestType guest,
                                            const DeviceHandler& handler) = 0;
  virtual void RegisterDefaultInterfaceChangedHandler(
      GuestMessage::GuestType guest, const NameHandler& handler) = 0;
  virtual void RegisterDeviceIPv6AddressFoundHandler(
      GuestMessage::GuestType guest, const DeviceHandler& handler) = 0;

  // Invoked when a guest starts or stops.
  virtual void OnGuestStart(GuestMessage::GuestType guest) = 0;
  virtual void OnGuestStop(GuestMessage::GuestType guest) = 0;

  // Used by a guest service to examine and potentially mutate tracked devices.
  virtual void ProcessDevices(const DeviceHandler& handler) = 0;

  // Returns whether a particular device is tracked. For testing only.
  virtual bool Exists(const std::string& name) const = 0;

  virtual Device* FindByHostInterface(const std::string& ifname) const = 0;
  virtual Device* FindByGuestInterface(const std::string& ifname) const = 0;

  virtual const std::string& DefaultInterface() const = 0;

  virtual bool Add(const std::string& name) = 0;
  virtual bool AddWithContext(const std::string& name,
                              std::unique_ptr<Device::Context>) = 0;
  virtual bool Remove(const std::string& name) = 0;
};

// Convenience class for managing the collection of host devices and their
// presentation in the container.
// Virtual methods for testing.
class DeviceManager : public DeviceManagerBase {
 public:
  // |addr_mgr| and |datapath| must not be null.
  DeviceManager(std::unique_ptr<ShillClient> shill_client,
                AddressManager* addr_mgr,
                Datapath* datapath,
                HelperProcess* mcast_proxy,
                HelperProcess* nd_proxy = nullptr);
  virtual ~DeviceManager();

  // Provided as part of DeviceManager for testing.
  virtual bool IsMulticastInterface(const std::string& ifname) const;

  // Used by guest service implementations to be notified when tracked devices
  // are updated.
  void RegisterDeviceAddedHandler(GuestMessage::GuestType guest,
                                  const DeviceHandler& handler) override;
  void RegisterDeviceRemovedHandler(GuestMessage::GuestType guest,
                                    const DeviceHandler& handler) override;
  void RegisterDefaultInterfaceChangedHandler(
      GuestMessage::GuestType guest, const NameHandler& handler) override;
  void RegisterDeviceIPv6AddressFoundHandler(
      GuestMessage::GuestType guest, const DeviceHandler& handler) override;

  // Invoked when a guest starts or stops.
  void OnGuestStart(GuestMessage::GuestType guest) override;
  void OnGuestStop(GuestMessage::GuestType guest) override;

  void OnDeviceMessageFromNDProxy(const DeviceMessage& msg);

  // Used by a guest service to examine and potentially mutate tracked devices.
  void ProcessDevices(const DeviceHandler& handler) override;

  // Returns whether a particular device is tracked. For testing only.
  bool Exists(const std::string& name) const override;

  Device* FindByHostInterface(const std::string& ifname) const override;
  Device* FindByGuestInterface(const std::string& ifname) const override;

  const std::string& DefaultInterface() const override;

  // Returns a new fully configured device.
  // Note this is public mainly for testing and should not be called directly.
  std::unique_ptr<Device> MakeDevice(const std::string& name) const;

  // Adds a new device by name. Returns whether the device was successfully
  // added.
  bool Add(const std::string& name) override;
  bool AddWithContext(const std::string& name,
                      std::unique_ptr<Device::Context>) override;
  bool Remove(const std::string& name) override;

 private:
  // Callback from ShillClient, invoked whenever the default network
  // interface changes or goes away.
  void OnDefaultInterfaceChanged(const std::string& ifname);

  // Callback from ShillClient, invoked whenever the device list changes.
  // |devices| will contain all devices currently connected to shill
  // (e.g. "eth0", "wlan0", etc).
  void OnDevicesChanged(const std::set<std::string>& devices);

  // Callback from Device, invoked when an IPv6 address is discovered and
  // assigned to the device.
  void OnIPv6AddressFound(Device* device);

  // Callback from RTNetlink listener, invoked when an interface changes
  // run state.
  void LinkMsgHandler(const shill::RTNLMessage& msg);

  // Listens for RTMGRP_LINK messages and invokes LinkMsgHandler.
  std::unique_ptr<shill::RTNLListener> link_listener_;

  // Receives network device updates and notifications from shill.
  std::unique_ptr<ShillClient> shill_client_;

  // Provisions and tracks subnets and addresses.
  AddressManager* addr_mgr_;

  // Callbacks for notifying clients when devices are added or removed from
  // |devices_|.
  std::map<GuestMessage::GuestType, DeviceHandler> add_handlers_;
  std::map<GuestMessage::GuestType, DeviceHandler> rm_handlers_;
  std::map<GuestMessage::GuestType, DeviceHandler> ipv6_handlers_;
  std::map<GuestMessage::GuestType, NameHandler> default_iface_handlers_;

  // Connected devices keyed by the interface name.
  // The legacy device is mapped to the Android interface name.
  std::map<std::string, std::unique_ptr<Device>> devices_;

  Datapath* datapath_;
  HelperProcess* mcast_proxy_;
  HelperProcess* nd_proxy_;

  std::string default_ifname_;

  base::WeakPtrFactory<DeviceManager> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(DeviceManager);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_DEVICE_MANAGER_H_

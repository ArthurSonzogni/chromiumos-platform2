// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_DEVICE_H_
#define SHILL_WIFI_P2P_DEVICE_H_

#include <optional>
#include <string>

#include "shill/wifi/local_device.h"

namespace shill {

class Manager;

class P2PDevice : public LocalDevice {
 public:
  enum class P2PDeviceState {
    // Common states for all roles.
    kUninitialized,  // P2PDevice instance created, but no interface is created
                     // in kernel
    kReady,  // Any prerequisite steps (like connect to the primary interface,
             // get up to date phy info) are done on the device and can start
             // the P2P process

    // P2P client states.
    kClientAssociating,  // P2P client is connecting to a group
    kClientConfiguring,  // P2P client has joined an L2 P2P group and is setting
                         // up L3 connectivity
    kClientConnected,    // P2P client has joined a group and L3 link has been
                         // established
    kClientDisconnecting,  // P2P client is disconnecting from a group

    // P2P GO states.
    kGOStarting,     // P2P GO is creating a group
    kGOConfiguring,  // P2P GO has created an L2 P2P group and is setting up L3
                     // network
    kGOActive,       // P2P GO has created a group and can accept connections
    kGOStopping,     // P2P GO is destroying a group
  };

  // Constructor function
  P2PDevice(Manager* manager,
            LocalDevice::IfaceType iface_type,
            const std::string& primary_link_name,
            uint32_t phy_index,
            uint32_t shill_id,
            LocalDevice::EventCallback callback);

  P2PDevice(const P2PDevice&) = delete;
  P2PDevice& operator=(const P2PDevice&) = delete;

  ~P2PDevice() override;

  static const char* P2PDeviceStateName(P2PDeviceState state);

  // P2PDevice start routine. Override the base class Start.
  bool Start() override;

  // P2PDevice stop routine. Override the base class Stop.
  bool Stop() override;

  // Stubbed to return null.
  LocalService* GetService() const override { return nullptr; }

  // Set device link_name;
  void SetLinkName(std::string link_name) { link_name_ = link_name; }

  // Set P2PDeviceState.
  void SetState(P2PDeviceState state);

  // Get shill_id_.
  uint32_t shill_id() { return shill_id_; }

 private:
  friend class P2PDeviceTest;
  FRIEND_TEST(P2PDeviceTest, DeviceOnOff);

  // Primary interface link name.
  std::string primary_link_name_;

  // Uniquely identifies this device relative to all other P2P devices in Shill.
  uint32_t shill_id_;
  // P2P device state as listed in enum P2PDeviceState.
  P2PDeviceState state_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_DEVICE_H_

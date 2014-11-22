// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APMANAGER_DEVICE_INFO_H_
#define APMANAGER_DEVICE_INFO_H_

#include <map>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "apmanager/device.h"

namespace shill {

class NetlinkManager;
class Nl80211Message;
class RTNLHandler;
class RTNLMessage;
class RTNLListener;
class Sockets;

}  // namespace shill

namespace apmanager {

class Manager;

// DeviceInfo will enumerate WiFi devices (PHYs) during startup, and use RTNL
// to monitor creation/deletion of WiFi interfaces. Currently, we only enumerate
// WiFi devices during startup, which would cause the WiFi devices to not get
// enumerated if apmanager is started before WiFi drivers are loaded.
// TODO(zqiu): add support for on-demand WiFi device enumeration, which will
// enumerate WiFi device when interface is detected on a phy that has not been
// enumerate.
class DeviceInfo : public base::SupportsWeakPtr<DeviceInfo> {
 public:
  explicit DeviceInfo(Manager* manager);
  virtual ~DeviceInfo();

  // Start and stop device detection monitoring.
  void Start();
  void Stop();

 private:
  friend class DeviceInfoTest;

  static const char kDeviceInfoRoot[];
  static const char kInterfaceUevent[];
  static const char kInterfaceUeventWifiSignature[];

  // Use nl80211 to enumerate available WiFi PHYs.
  void EnumerateDevices();
  void OnWiFiPhyInfoReceived(const shill::Nl80211Message& msg);

  // Handler for RTNL link event.
  void LinkMsgHandler(const shill::RTNLMessage& msg);
  void AddLinkMsgHandler(const std::string& iface_name, int iface_index);
  void DelLinkMsgHandler(const std::string& iface_name, int iface_index);

  // Return true if the specify |iface_name| is a wifi interface, false
  // otherwise.
  bool IsWifiInterface(const std::string& iface_name);

  // Return the contents of the device info file |path_name| for interface
  // |iface_name| in output parameter |contents_out|. Return true if file
  // read succeed, fales otherwise.
  bool GetDeviceInfoContents(const std::string& iface_name,
                             const std::string& path_name,
                             std::string* contents_out);

  // Use nl80211 to get WiFi interface information for interface on
  // |iface_index|.
  void GetWiFiInterfaceInfo(int iface_index);
  void OnWiFiInterfaceInfoReceived(const shill::Nl80211Message& msg);

  // Use nl80211 to get PHY info for interface on |iface_index|.
  void GetWiFiInterfacePhyInfo(uint32_t iface_index);
  void OnWiFiInterfacePhyInfoReceived(
      uint32_t iface_index, const shill::Nl80211Message& msg);

  scoped_refptr<Device> GetDevice(const std::string& phy_name);
  void RegisterDevice(scoped_refptr<Device> device);

  // Maps interface index to interface info
  std::map<uint32_t, Device::WiFiInterface> interface_infos_;
  // Maps device name to device object. Each device object represents a PHY.
  std::map<std::string, scoped_refptr<Device>> devices_;

  // RTNL link event callback and listener.
  base::Callback<void(const shill::RTNLMessage&)> link_callback_;
  std::unique_ptr<shill::RTNLListener> link_listener_;

  base::FilePath device_info_root_;
  Manager *manager_;

  // Cache copy of singleton pointers.
  shill::NetlinkManager* netlink_manager_;
  shill::RTNLHandler* rtnl_handler_;

  std::unique_ptr<shill::Sockets> sockets_;

  DISALLOW_COPY_AND_ASSIGN(DeviceInfo);
};

}  // namespace apmanager

#endif  // APMANAGER_DEVICE_INFO_H_

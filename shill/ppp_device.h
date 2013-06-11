// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PPP_DEVICE_H_
#define SHILL_PPP_DEVICE_H_

#include <base/basictypes.h>

#include <map>
#include <string>

#include "shill/ipconfig.h"
#include "shill/virtual_device.h"

namespace shill {

// Declared in the header to avoid linking unused code into shims.
static const char kPPPDNS1[] = "DNS1";
static const char kPPPDNS2[] = "DNS2";
static const char kPPPExternalIP4Address[] = "EXTERNAL_IP4_ADDRESS";
static const char kPPPGatewayAddress[] = "GATEWAY_ADDRESS";
static const char kPPPInterfaceName[] = "INTERNAL_IFNAME";
static const char kPPPInternalIP4Address[] = "INTERNAL_IP4_ADDRESS";
static const char kPPPLNSAddress[] = "LNS_ADDRESS";
static const char kPPPReasonConnect[] = "connect";
static const char kPPPReasonDisconnect[] = "disconnect";

class PPPDevice : public VirtualDevice {
 public:
  static const char kDaemonPath[];
  static const char kPluginPath[];

  PPPDevice(ControlInterface *control,
            EventDispatcher *dispatcher,
            Metrics *metrics,
            Manager *manager,
            const std::string &link_name,
            int interface_index);
  virtual ~PPPDevice();

  // Set IPConfig for this device, based on the dictionary of
  // configuration strings received from our PPP plugin.
  virtual void UpdateIPConfigFromPPP(
      const std::map<std::string, std::string> &configuration,
      bool blackhole_ipv6);

  // Get the network device name (e.g. "ppp0") from the dictionary of
  // configuration strings received from our PPP plugin.
  static std::string GetInterfaceName(
      const std::map<std::string, std::string> &configuration);

 private:
  FRIEND_TEST(PPPDeviceTest, GetInterfaceName);
  FRIEND_TEST(PPPDeviceTest, ParseIPConfiguration);

  static void ParseIPConfiguration(
      const std::string &link_name,
      const std::map<std::string, std::string> &configuration,
      IPConfig::Properties *properties);

  DISALLOW_COPY_AND_ASSIGN(PPPDevice);
};

}  // namespace shill

#endif  // SHILL_PPP_DEVICE_H_

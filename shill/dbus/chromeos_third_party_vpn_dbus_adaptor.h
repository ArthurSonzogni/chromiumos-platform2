// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_THIRD_PARTY_VPN_DBUS_ADAPTOR_H_
#define SHILL_DBUS_CHROMEOS_THIRD_PARTY_VPN_DBUS_ADAPTOR_H_

#include <map>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/scoped_ptr.h>

#include "shill/adaptor_interfaces.h"
#include "shill/chromeos_dbus_adaptors/org.chromium.flimflam.ThirdPartyVpn.h"
#include "shill/dbus/chromeos_dbus_adaptor.h"

namespace shill {

class ThirdPartyVpnDriver;

class ChromeosThirdPartyVpnDBusAdaptor
    : public org::chromium::flimflam::ThirdPartyVpnAdaptor,
      public org::chromium::flimflam::ThirdPartyVpnInterface,
      public ChromeosDBusAdaptor,
      public ThirdPartyVpnAdaptorInterface {
 public:
  enum ExternalConnectState {
    kStateConnected = 1,
    kStateFailure,
  };

  ChromeosThirdPartyVpnDBusAdaptor(
      const base::WeakPtr<chromeos::dbus_utils::ExportedObjectManager>&
          object_manager,
      const scoped_refptr<dbus::Bus>& bus,
      ThirdPartyVpnDriver* client);
  ~ChromeosThirdPartyVpnDBusAdaptor() override;

  // Implementation of ThirdPartyVpnAdaptorInterface
  void EmitPacketReceived(const std::vector<uint8_t>& packet) override;
  void EmitPlatformMessage(uint32_t message) override;

  // Implementation of org::chromium::flimflam::ThirdPartyVpnAdaptor
  bool SetParameters(
      chromeos::ErrorPtr* error,
      const std::map<std::string, std::string>& parameters,
      std::string* warning_message) override;
  bool UpdateConnectionState(chromeos::ErrorPtr* error,
                             uint32_t connection_state) override;
  bool SendPacket(chromeos::ErrorPtr* error,
                  const std::vector<uint8_t>& ip_packet) override;

 private:
  ThirdPartyVpnDriver* client_;

  DISALLOW_COPY_AND_ASSIGN(ChromeosThirdPartyVpnDBusAdaptor);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_THIRD_PARTY_VPN_DBUS_ADAPTOR_H_

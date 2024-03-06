// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_peer.h"

#include <chromeos/dbus/shill/dbus-constants.h>
#include <net-base/mac_address.h>

#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/p2p_device.h"

namespace shill {

P2PPeer::P2PPeer(P2PDeviceConstRefPtr device,
                 const dbus::ObjectPath& peer,
                 ControlInterface* control_interface)
    : p2p_device_(device), supplicant_peer_path_(RpcIdentifier(peer.value())) {
  supplicant_peer_proxy_ =
      control_interface->CreateSupplicantPeerProxy(supplicant_peer_path_);

  KeyValueStore properties;
  supplicant_peer_proxy_->GetProperties(&properties);

  if (properties.Contains<ByteArray>(
          WPASupplicant::kPeerPropertyDeviceAddress)) {
    mac_address_ = net_base::MacAddress::CreateFromBytes(
                       properties.Get<ByteArray>(
                           WPASupplicant::kPeerPropertyDeviceAddress))
                       ->ToString();
  }
}

P2PPeer::~P2PPeer() {
  supplicant_peer_proxy_.reset();
}

Stringmap P2PPeer::GetPeerProperties() {
  Stringmap client;
  client.insert({kP2PGroupInfoClientMACAddressProperty, mac_address_});
  // TODO(b/299915001): retrieve IPv4/IPv6Address and Hostname from patchpanel
  // TODO(b/301049348): retrieve vendor class from wpa_supplicant
  return client;
}

}  // namespace shill

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_PEER_H_
#define SHILL_WIFI_P2P_PEER_H_

#include "shill/control_interface.h"
#include "shill/refptr_types.h"
#include "shill/store/key_value_store.h"
#include "shill/supplicant/supplicant_peer_proxy_interface.h"

namespace shill {
// A P2PPeer represents a wpa_supplicant network in mode P2P.
class P2PPeer {
 public:
  P2PPeer(P2PDeviceConstRefPtr device,
          const dbus::ObjectPath& peer,
          ControlInterface* control_interface);
  P2PPeer(const P2PPeer&) = delete;
  P2PPeer& operator=(const P2PPeer&) = delete;
  virtual ~P2PPeer();

  // Return a dictionary of peer info for GroupInfo property.
  Stringmap GetPeerProperties();

 private:
  friend class P2PPeerTest;
  FRIEND_TEST(P2PPeerTest, GetPeerProperties);

  P2PDeviceConstRefPtr p2p_device_;
  RpcIdentifier supplicant_peer_path_;
  std::unique_ptr<SupplicantPeerProxyInterface> supplicant_peer_proxy_;

  std::string mac_address_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_PEER_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_SUPPLICANT_PEER_PROXY_INTERFACE_H_
#define SHILL_SUPPLICANT_SUPPLICANT_PEER_PROXY_INTERFACE_H_

namespace shill {

// SupplicantPeerProxyInterface declares only the subset of
// fi::w1::wpa_supplicant1::Peer_proxy that is actually used by WiFi P2P.
class SupplicantPeerProxyInterface {
 public:
  virtual ~SupplicantPeerProxyInterface() = default;
  virtual bool GetProperties(KeyValueStore* properties) = 0;
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_SUPPLICANT_PEER_PROXY_INTERFACE_H_

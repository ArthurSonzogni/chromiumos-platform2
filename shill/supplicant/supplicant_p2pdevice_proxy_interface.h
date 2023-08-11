// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_SUPPLICANT_P2PDEVICE_PROXY_INTERFACE_H_
#define SHILL_SUPPLICANT_SUPPLICANT_P2PDEVICE_PROXY_INTERFACE_H_

#include <string>

#include "shill/store/key_value_store.h"

namespace shill {

// SupplicantP2PDeviceProxyInterface declares only the subset of
// fi::w1::wpa_supplicant1::Interface::P2PDeviceProxy that is actually used by
// WiFi P2P.
class SupplicantP2PDeviceProxyInterface {
 public:
  virtual ~SupplicantP2PDeviceProxyInterface() = default;
  virtual bool GroupAdd(const KeyValueStore& args) = 0;
  virtual bool Disconnect() = 0;
  virtual bool AddPersistentGroup(const KeyValueStore& args,
                                  RpcIdentifier* rpc_identifier) = 0;
  virtual bool RemovePersistentGroup(const RpcIdentifier& rpc_identifier) = 0;
  virtual bool GetDeviceConfig(KeyValueStore* config) = 0;
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_SUPPLICANT_P2PDEVICE_PROXY_INTERFACE_H_

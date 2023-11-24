// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_MOCK_SUPPLICANT_P2PDEVICE_PROXY_H_
#define SHILL_SUPPLICANT_MOCK_SUPPLICANT_P2PDEVICE_PROXY_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/refptr_types.h"
#include "shill/supplicant/supplicant_p2pdevice_proxy_interface.h"

namespace shill {

class MockSupplicantP2PDeviceProxy : public SupplicantP2PDeviceProxyInterface {
 public:
  MockSupplicantP2PDeviceProxy();
  MockSupplicantP2PDeviceProxy(const MockSupplicantP2PDeviceProxy&) = delete;
  MockSupplicantP2PDeviceProxy& operator=(const MockSupplicantP2PDeviceProxy&) =
      delete;

  ~MockSupplicantP2PDeviceProxy() override;

  MOCK_METHOD(bool, GroupAdd, (const KeyValueStore&), (override));
  MOCK_METHOD(bool, Disconnect, (), (override));
  MOCK_METHOD(bool,
              AddPersistentGroup,
              (const KeyValueStore&, RpcIdentifier*),
              (override));
  MOCK_METHOD(bool, RemovePersistentGroup, (const RpcIdentifier&), (override));
  MOCK_METHOD(bool, GetDeviceConfig, (KeyValueStore * config), (override));
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_MOCK_SUPPLICANT_P2PDEVICE_PROXY_H_

// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_WIMAX_DEVICE_PROXY_H_
#define SHILL_MOCK_WIMAX_DEVICE_PROXY_H_

#include <base/basictypes.h>
#include <gmock/gmock.h>

#include "shill/wimax_device_proxy_interface.h"

namespace shill {

class MockWiMaxDeviceProxy : public WiMaxDeviceProxyInterface {
 public:
  MockWiMaxDeviceProxy();
  virtual ~MockWiMaxDeviceProxy();

  MOCK_METHOD3(Enable, void(Error *error,
                            const ResultCallback &callback,
                            int timeout));
  MOCK_METHOD3(Disable, void(Error *error,
                             const ResultCallback &callback,
                             int timeout));
  MOCK_METHOD3(ScanNetworks, void(Error *error,
                                  const ResultCallback &callback,
                                  int timeout));
  MOCK_METHOD5(Connect, void(const DBus::Path &network,
                             const DBusPropertiesMap &parameters,
                             Error *error,
                             const ResultCallback &callback,
                             int timeout));
  MOCK_METHOD3(Disconnect, void(Error *error,
                                const ResultCallback &callback,
                                int timeout));
  MOCK_METHOD1(set_networks_changed_callback,
               void(const NetworksChangedCallback &callback));
  MOCK_METHOD1(Index, uint8(Error *error));
  MOCK_METHOD1(Name, std::string(Error *error));

  DISALLOW_COPY_AND_ASSIGN(MockWiMaxDeviceProxy);
};

}  // namespace shill

#endif  // SHILL_MOCK_WIMAX_DEVICE_PROXY_H_

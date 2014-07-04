// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_DEVICE_H_
#define SHILL_MOCK_DEVICE_H_

#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <gmock/gmock.h>

#include "shill/device.h"
#include "shill/geolocation_info.h"

namespace shill {

class MockDevice : public Device {
 public:
  MockDevice(ControlInterface *control_interface,
             EventDispatcher *dispatcher,
             Metrics *metrics,
             Manager *manager,
             const std::string &link_name,
             const std::string &address,
             int interface_index);
  virtual ~MockDevice();

  MOCK_METHOD2(Start, void(Error *error,
                           const EnabledStateChangedCallback &callback));
  MOCK_METHOD2(Stop, void(Error *error,
                          const EnabledStateChangedCallback &callback));
  MOCK_METHOD1(SetEnabled, void(bool));
  MOCK_METHOD3(SetEnabledPersistent, void(bool enable,
                                          Error* error,
                                          const ResultCallback &callback));
  MOCK_METHOD3(Scan, void(Device::ScanType scan_type, Error *error,
                          const std::string &reason));
  MOCK_METHOD1(Load, bool(StoreInterface *storage));
  MOCK_METHOD1(Save, bool(StoreInterface *storage));
  MOCK_METHOD0(DisableIPv6, void());
  MOCK_METHOD0(EnableIPv6, void());
  MOCK_METHOD0(EnableIPv6Privacy, void());
  MOCK_METHOD0(DisableReversePathFilter, void());
  MOCK_METHOD0(EnableReversePathFilter, void());
  MOCK_METHOD0(RestartPortalDetection, bool());
  MOCK_METHOD0(RequestPortalDetection, bool());
  MOCK_METHOD0(GetReceiveByteCount, uint64());
  MOCK_METHOD0(GetTransmitByteCount, uint64());
  MOCK_CONST_METHOD1(IsConnectedToService, bool(const ServiceRefPtr &service));
  MOCK_CONST_METHOD0(technology, Technology::Identifier());
  MOCK_METHOD0(OnBeforeSuspend, void());
  MOCK_METHOD0(OnAfterResume, void());
  MOCK_METHOD0(OnConnectionUpdated, void());
  MOCK_METHOD0(OnIPv6AddressChanged, void());
  MOCK_CONST_METHOD0(GetGeolocationObjects, std::vector<GeolocationInfo>());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDevice);
};

}  // namespace shill

#endif  // SHILL_MOCK_DEVICE_H_

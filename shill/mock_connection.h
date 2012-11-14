// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CONNECTION_H_
#define SHILL_MOCK_CONNECTION_H_

#include <base/basictypes.h>
#include <gmock/gmock.h>

#include "shill/connection.h"

namespace shill {

class MockConnection : public Connection {
 public:
  MockConnection(const DeviceInfo *device_info);
  virtual ~MockConnection();

  MOCK_METHOD1(UpdateFromIPConfig, void(const IPConfigRefPtr &config));
  MOCK_CONST_METHOD0(GetLowerConnection, ConnectionRefPtr());
  MOCK_CONST_METHOD0(is_default, bool());
  MOCK_METHOD1(SetIsDefault, void(bool is_default));
  MOCK_CONST_METHOD0(ipconfig_rpc_identifier, const std::string &());
  MOCK_METHOD0(RequestRouting, void());
  MOCK_METHOD0(ReleaseRouting, void());
  MOCK_CONST_METHOD0(interface_name, const std::string &());
  MOCK_CONST_METHOD0(dns_servers, const std::vector<std::string> &());
  MOCK_METHOD1(RequestHostRoute, bool(const IPAddress &destination));
  MOCK_CONST_METHOD0(local, const IPAddress &());
  MOCK_CONST_METHOD0(gateway, const IPAddress &());
  MOCK_METHOD0(CreateGatewayRoute, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockConnection);
};

}  // namespace shill

#endif  // SHILL_MOCK_CONNECTION_H_

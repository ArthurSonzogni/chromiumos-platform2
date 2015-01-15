// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_MOCK_RTNL_HANDLER_H_
#define SHILL_NET_MOCK_RTNL_HANDLER_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/net/rtnl_handler.h"

namespace shill {

class MockRTNLHandler : public RTNLHandler {
 public:
  MockRTNLHandler() {}
  ~MockRTNLHandler() override {}

  MOCK_METHOD0(Start, void());
  MOCK_METHOD1(AddListener, void(RTNLListener *to_add));
  MOCK_METHOD1(RemoveListener, void(RTNLListener *to_remove));
  MOCK_METHOD3(SetInterfaceFlags, void(int interface_index,
                                       unsigned int flags,
                                       unsigned int change));
  MOCK_METHOD4(AddInterfaceAddress, bool(int interface_index,
                                         const IPAddress &local,
                                         const IPAddress &broadcast,
                                         const IPAddress &peer));
  MOCK_METHOD2(RemoveInterfaceAddress, bool(int interface_index,
                                            const IPAddress &local));
  MOCK_METHOD1(RemoveInterface, bool(int interface_index));
  MOCK_METHOD1(RequestDump, void(int request_flags));
  MOCK_METHOD1(GetInterfaceIndex, int(const std::string &interface_name));
  MOCK_METHOD1(SendMessage, bool(RTNLMessage *message));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockRTNLHandler);
};

}  // namespace shill

#endif  // SHILL_NET_MOCK_RTNL_HANDLER_H_

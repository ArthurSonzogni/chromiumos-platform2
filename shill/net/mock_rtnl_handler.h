// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_MOCK_RTNL_HANDLER_H_
#define SHILL_NET_MOCK_RTNL_HANDLER_H_

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <net-base/mac_address.h>

#include "shill/net/rtnl_handler.h"

namespace shill {

class MockRTNLHandler : public RTNLHandler {
 public:
  MockRTNLHandler();
  MockRTNLHandler(const MockRTNLHandler&) = delete;
  MockRTNLHandler& operator=(const MockRTNLHandler&) = delete;

  ~MockRTNLHandler() override;

  MOCK_METHOD(void, Start, (uint32_t), (override));
  MOCK_METHOD(void, AddListener, (RTNLListener*), (override));
  MOCK_METHOD(void, RemoveListener, (RTNLListener*), (override));
  MOCK_METHOD(void,
              SetInterfaceFlags,
              (int, unsigned int, unsigned int),
              (override));
  MOCK_METHOD(void, SetInterfaceMTU, (int, unsigned int), (override));
  MOCK_METHOD(void,
              SetInterfaceMac,
              (int, const net_base::MacAddress&, ResponseCallback),
              (override));
  MOCK_METHOD(bool,
              AddInterfaceAddress,
              (int,
               const net_base::IPCIDR&,
               const std::optional<net_base::IPv4Address>&),
              (override));
  MOCK_METHOD(bool,
              RemoveInterfaceAddress,
              (int, const net_base::IPCIDR&),
              (override));
  MOCK_METHOD(bool, RemoveInterface, (int), (override));
  MOCK_METHOD(void, RequestDump, (uint32_t), (override));
  MOCK_METHOD(int, GetInterfaceIndex, (const std::string&), (override));
  MOCK_METHOD(bool, DoSendMessage, (net_base::RTNLMessage*, uint32_t*));
  MOCK_METHOD(bool,
              AddInterface,
              (const std::string& interface_name,
               const std::string& link_kind,
               base::span<const uint8_t> link_info_data,
               ResponseCallback response_callback),
              (override));
  bool SendMessage(std::unique_ptr<net_base::RTNLMessage> message,
                   uint32_t* seq) override {
    return DoSendMessage(message.get(), seq);
  }
};

}  // namespace shill

#endif  // SHILL_NET_MOCK_RTNL_HANDLER_H_

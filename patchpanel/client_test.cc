// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/client.h"

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

using ::testing::_;
using ::testing::ByMove;
using ::testing::Return;

namespace {

scoped_refptr<dbus::MockBus> MockDBus() {
  return new dbus::MockBus{dbus::Bus::Options{}};
}

scoped_refptr<dbus::MockObjectProxy> PatchPanelMockProxy(dbus::MockBus* dbus) {
  return new dbus::MockObjectProxy(dbus, kPatchPanelServiceName,
                                   dbus::ObjectPath(kPatchPanelServicePath));
}

}  // namespace

TEST(ClientTest, ConnectNamespace) {
  auto dbus = MockDBus();
  auto proxy = PatchPanelMockProxy(dbus.get());
  pid_t pid = 3456;
  std::string outboud_ifname = "";

  Client client(dbus, proxy.get());

  // Failure case
  auto result = client.ConnectNamespace(pid, outboud_ifname, false);
  EXPECT_FALSE(result.first.is_valid());
  EXPECT_TRUE(result.second.ifname().empty());
  EXPECT_EQ(0, result.second.ipv4_subnet().base_addr());
  EXPECT_EQ(0, result.second.ipv4_subnet().prefix_len());
  EXPECT_EQ(0, result.second.ipv4_address());

  // Success case
  patchpanel::ConnectNamespaceResponse response_proto;
  response_proto.set_ifname("arc_ns0");
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_prefix_len(30);
  response_subnet->set_base_addr(Ipv4Addr(100, 115, 92, 128));
  response_proto.set_ipv4_address(Ipv4Addr(100, 115, 92, 130));
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  dbus::MessageWriter response_writer(response.get());
  response_writer.AppendProtoAsArrayOfBytes(response_proto);
  EXPECT_CALL(*proxy, CallMethodAndBlock(_, _))
      .WillOnce(Return(ByMove(std::move(response))));

  result = client.ConnectNamespace(pid, outboud_ifname, false);
  EXPECT_TRUE(result.first.is_valid());
  EXPECT_EQ("arc_ns0", result.second.ifname());
  EXPECT_EQ(30, result.second.ipv4_subnet().prefix_len());
  EXPECT_EQ(Ipv4Addr(100, 115, 92, 128),
            result.second.ipv4_subnet().base_addr());
  EXPECT_EQ(Ipv4Addr(100, 115, 92, 130), result.second.ipv4_address());
}

}  // namespace patchpanel

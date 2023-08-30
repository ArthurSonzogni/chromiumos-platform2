// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/system.h"

#include <base/logging.h>
#include <gtest/gtest.h>

namespace patchpanel {
namespace {

TEST(SystemTest, SysNetPath) {
  System system;

  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv4Forward),
            "/proc/sys/net/ipv4/ip_forward");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPLocalPortRange),
            "/proc/sys/net/ipv4/ip_local_port_range");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv4RouteLocalnet, "eth0"),
            "/proc/sys/net/ipv4/conf/eth0/route_localnet");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6AcceptRA, "eth0"),
            "/proc/sys/net/ipv6/conf/eth0/accept_ra");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6Forward),
            "/proc/sys/net/ipv6/conf/all/forwarding");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kConntrackHelper),
            "/proc/sys/net/netfilter/nf_conntrack_helper");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6Disable),
            "/proc/sys/net/ipv6/conf/all/disable_ipv6");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6ProxyNDP),
            "/proc/sys/net/ipv6/conf/all/proxy_ndp");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6HopLimit, "eth0"),
            "/proc/sys/net/ipv6/conf/eth0/hop_limit");

  // Failure cases.
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv4RouteLocalnet, ""), "");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6AcceptRA, ""), "");
  EXPECT_EQ(system.SysNetPath(System::SysNet::kIPv6HopLimit, ""), "");
}

}  // namespace
}  // namespace patchpanel

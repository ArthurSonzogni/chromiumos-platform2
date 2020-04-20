// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/datapath.h"

#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/net_util.h"

using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::StrEq;

namespace patchpanel {
namespace {

// TODO(hugobenichi) Centralize this constant definition
constexpr pid_t kTestPID = -2;

std::vector<ioctl_req_t> ioctl_reqs;
std::vector<std::pair<std::string, struct rtentry>> ioctl_rtentry_args;

// Capture all ioctls and succeed.
int ioctl_req_cap(int fd, ioctl_req_t req, ...) {
  ioctl_reqs.push_back(req);
  return 0;
}

// Capture ioctls for SIOCADDRT and SIOCDELRT and succeed.
int ioctl_rtentry_cap(int fd, ioctl_req_t req, struct rtentry* arg) {
  ioctl_reqs.push_back(req);
  ioctl_rtentry_args.push_back({"", *arg});
  // Copy the string poited by rtentry.rt_dev because Add/DeleteIPv4Route pass
  // this value to ioctl() on the stack.
  if (arg->rt_dev) {
    auto& cap = ioctl_rtentry_args.back();
    cap.first = std::string(arg->rt_dev);
    cap.second.rt_dev = (char*)cap.first.c_str();
  }
  return 0;
}

}  // namespace

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner() = default;
  ~MockProcessRunner() = default;

  MOCK_METHOD2(RestoreDefaultNamespace,
               int(const std::string& ifname, pid_t pid));
  MOCK_METHOD1(WriteSentinelToContainer, int(pid_t pid));
  MOCK_METHOD3(brctl,
               int(const std::string& cmd,
                   const std::vector<std::string>& argv,
                   bool log_failures));
  MOCK_METHOD4(chown,
               int(const std::string& uid,
                   const std::string& gid,
                   const std::string& file,
                   bool log_failures));
  MOCK_METHOD4(ip,
               int(const std::string& obj,
                   const std::string& cmd,
                   const std::vector<std::string>& args,
                   bool log_failures));
  MOCK_METHOD4(ip6,
               int(const std::string& obj,
                   const std::string& cmd,
                   const std::vector<std::string>& args,
                   bool log_failures));
  MOCK_METHOD3(iptables,
               int(const std::string& table,
                   const std::vector<std::string>& argv,
                   bool log_failures));
  MOCK_METHOD3(ip6tables,
               int(const std::string& table,
                   const std::vector<std::string>& argv,
                   bool log_failures));
  MOCK_METHOD2(modprobe_all,
               int(const std::vector<std::string>& modules, bool log_failures));
  MOCK_METHOD3(sysctl_w,
               int(const std::string& key,
                   const std::string& value,
                   bool log_failures));
};

TEST(DatapathTest, AddTAP) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(Ipv4Addr(100, 115, 92, 4), 30, base::DoNothing());
  auto addr = subnet.AllocateAtOffset(0);
  auto ifname = datapath.AddTAP("foo0", &mac, addr.get(), "");
  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,     TUNSETPERSIST, SIOCSIFADDR, SIOCSIFNETMASK,
      SIOCSIFHWADDR, SIOCGIFFLAGS,  SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, AddTAPWithOwner) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(Ipv4Addr(100, 115, 92, 4), 30, base::DoNothing());
  auto addr = subnet.AllocateAtOffset(0);
  auto ifname = datapath.AddTAP("foo0", &mac, addr.get(), "root");
  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,      TUNSETPERSIST, TUNSETOWNER,  SIOCSIFADDR,
      SIOCSIFNETMASK, SIOCSIFHWADDR, SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, AddTAPNoAddrs) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  auto ifname = datapath.AddTAP("foo0", nullptr, nullptr, "");
  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {TUNSETIFF, TUNSETPERSIST, SIOCGIFFLAGS,
                                       SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, RemoveTAP) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip(StrEq("tuntap"), StrEq("del"),
                         ElementsAre("foo0", "mode", "tap"), true));
  Datapath datapath(&runner);
  datapath.RemoveTAP("foo0");
}

TEST(DatapathTest, AddBridge) {
  MockProcessRunner runner;
  Datapath datapath(&runner);
  EXPECT_CALL(runner, brctl(StrEq("addbr"), ElementsAre("br"), true));
  EXPECT_CALL(
      runner,
      ip(StrEq("addr"), StrEq("add"),
         ElementsAre("1.1.1.1/30", "brd", "1.1.1.3", "dev", "br"), true));
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("br", "up"), true));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "br", "-j",
                                           "MARK", "--set-mark", "1", "-w"),
                               true));
  datapath.AddBridge("br", Ipv4Addr(1, 1, 1, 1), 30);
}

TEST(DatapathTest, ConnectVethPair) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("add"),
                         ElementsAre("veth_foo", "type", "veth", "peer", "name",
                                     "peer_foo"),
                         true));
  EXPECT_CALL(runner, ip(StrEq("addr"), StrEq("add"),
                         ElementsAre("100.115.92.169/30", "brd",
                                     "100.115.92.171", "dev", "peer_foo"),
                         true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("dev", "peer_foo", "up", "addr",
                                     "01:02:03:04:05:06", "multicast", "on"),
                         true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, RestoreDefaultNamespace(StrEq("veth_foo"), kTestPID));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("veth_foo", "up"), true));
  Datapath datapath(&runner);
  EXPECT_TRUE(datapath.ConnectVethPair(kTestPID, "veth_foo", "peer_foo",
                                       {1, 2, 3, 4, 5, 6},
                                       Ipv4Addr(100, 115, 92, 169), 30, true));
}

TEST(DatapathTest, AddVirtualInterfacePair) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("add"),
                         ElementsAre("veth_foo", "type", "veth", "peer", "name",
                                     "peer_foo"),
                         true));
  Datapath datapath(&runner);
  EXPECT_TRUE(datapath.AddVirtualInterfacePair("veth_foo", "peer_foo"));
}

TEST(DatapathTest, ToggleInterface) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("foo", "up"), true));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("bar", "down"), true));
  Datapath datapath(&runner);
  EXPECT_TRUE(datapath.ToggleInterface("foo", true));
  EXPECT_TRUE(datapath.ToggleInterface("bar", false));
}

TEST(DatapathTest, ConfigureInterface) {
  MockProcessRunner runner;
  EXPECT_CALL(
      runner,
      ip(StrEq("addr"), StrEq("add"),
         ElementsAre("1.1.1.1/30", "brd", "1.1.1.3", "dev", "foo"), true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("dev", "foo", "up", "addr",
                                     "02:02:02:02:02:02", "multicast", "on"),
                         true))
      .WillOnce(Return(0));

  Datapath datapath(&runner);
  MacAddress mac_addr = {2, 2, 2, 2, 2, 2};
  EXPECT_TRUE(datapath.ConfigureInterface("foo", mac_addr, Ipv4Addr(1, 1, 1, 1),
                                          30, true, true));
}

TEST(DatapathTest, RemoveInterface) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("delete"), ElementsAre("foo"), false));
  Datapath datapath(&runner);
  datapath.RemoveInterface("foo");
}

TEST(DatapathTest, RemoveBridge) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "br", "-j",
                                           "MARK", "--set-mark", "1", "-w"),
                               true));
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("br", "down"), true));
  EXPECT_CALL(runner, brctl(StrEq("delbr"), ElementsAre("br"), true));
  Datapath datapath(&runner);
  datapath.RemoveBridge("br");
}

TEST(DatapathTest, AddLegacyIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-N", "dnat_arc", "-w"), true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "dnat_arc", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-N", "try_arc", "-w"), true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-A", "PREROUTING", "-m", "socket",
                                   "--nowildcard", "-j", "ACCEPT", "-w"),
                       true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-p", "tcp",
                                           "-j", "try_arc", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-p", "udp",
                                           "-j", "try_arc", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddLegacyIPv4DNAT("1.2.3.4");
}

TEST(DatapathTest, RemoveLegacyIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-p", "udp",
                                           "-j", "try_arc", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-p", "tcp",
                                           "-j", "try_arc", "-w"),
                               true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-D", "PREROUTING", "-m", "socket",
                                   "--nowildcard", "-j", "ACCEPT", "-w"),
                       true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-F", "try_arc", "-w"), true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-X", "try_arc", "-w"), true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-F", "dnat_arc", "-w"), true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-X", "dnat_arc", "-w"), true));
  Datapath datapath(&runner);
  datapath.RemoveLegacyIPv4DNAT();
}

TEST(DatapathTest, AddLegacyIPv4InboundDNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "try_arc", "-i", "wlan0", "-j",
                                           "dnat_arc", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddLegacyIPv4InboundDNAT("wlan0");
}

TEST(DatapathTest, RemoveLegacyIPv4InboundDNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-F", "try_arc", "-w"), true));
  Datapath datapath(&runner);
  datapath.RemoveLegacyIPv4InboundDNAT();
}

TEST(DatapathTest, AddInboundIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, RemoveInboundIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.RemoveInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, AddOutboundIPv4) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-o", "eth0", "-j",
                                           "ACCEPT", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddOutboundIPv4("eth0");
}

TEST(DatapathTest, RemoveInboundIPv4) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-o", "eth0", "-j",
                                           "ACCEPT", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.RemoveOutboundIPv4("eth0");
}

TEST(DatapathTest, MaskInterfaceFlags) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  bool result = datapath.MaskInterfaceFlags("foo0", IFF_DEBUG);
  EXPECT_TRUE(result);
  std::vector<ioctl_req_t> expected = {SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, AddIPv6Forwarding) {
  MockProcessRunner runner;
  // Return 1 on iptables -C to simulate rule not existing case
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                false))
      .WillOnce(Return(1));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-A", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                true));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                false))
      .WillOnce(Return(1));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-A", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                true));
  Datapath datapath(&runner);
  datapath.AddIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6ForwardingRuleExists) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                false));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                false));
  Datapath datapath(&runner);
  datapath.AddIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, RemoveIPv6Forwarding) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-D", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                true));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-D", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                true));
  Datapath datapath(&runner);
  datapath.RemoveIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6HostRoute) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              ip6(StrEq("route"), StrEq("replace"),
                  ElementsAre("2001:da8:e00::1234/128", "dev", "eth0"), true));
  Datapath datapath(&runner);
  datapath.AddIPv6HostRoute("eth0", "2001:da8:e00::1234", 128);
}

TEST(DatapathTest, AddIPv4Route) {
  MockProcessRunner runner;
  Datapath datapath(&runner, (ioctl_t)ioctl_rtentry_cap);

  datapath.AddIPv4Route(Ipv4Addr(192, 168, 1, 1), Ipv4Addr(100, 115, 93, 0),
                        Ipv4Addr(255, 255, 255, 0));
  datapath.DeleteIPv4Route(Ipv4Addr(192, 168, 1, 1), Ipv4Addr(100, 115, 93, 0),
                           Ipv4Addr(255, 255, 255, 0));
  datapath.AddIPv4Route("eth0", Ipv4Addr(100, 115, 92, 8),
                        Ipv4Addr(255, 255, 255, 252));
  datapath.DeleteIPv4Route("eth0", Ipv4Addr(100, 115, 92, 8),
                           Ipv4Addr(255, 255, 255, 252));

  std::vector<ioctl_req_t> expected_reqs = {SIOCADDRT, SIOCDELRT, SIOCADDRT,
                                            SIOCDELRT};
  EXPECT_EQ(expected_reqs, ioctl_reqs);
  ioctl_reqs.clear();

  std::string route1 =
      "{rt_dst: {family: AF_INET, port: 0, addr: 100.115.93.0}, rt_genmask: "
      "{family: AF_INET, port: 0, addr: 255.255.255.0}, rt_gateway: {family: "
      "AF_INET, port: 0, addr: 192.168.1.1}, rt_dev: null, rt_flags: RTF_UP | "
      "RTF_GATEWAY}";
  std::string route2 =
      "{rt_dst: {family: AF_INET, port: 0, addr: 100.115.92.8}, rt_genmask: "
      "{family: AF_INET, port: 0, addr: 255.255.255.252}, rt_gateway: {unset}, "
      "rt_dev: eth0, rt_flags: RTF_UP | RTF_GATEWAY}";
  std::vector<std::string> captured_routes;
  for (const auto& route : ioctl_rtentry_args) {
    std::ostringstream stream;
    stream << route.second;
    captured_routes.emplace_back(stream.str());
  }
  ioctl_rtentry_args.clear();
  EXPECT_EQ(route1, captured_routes[0]);
  EXPECT_EQ(route1, captured_routes[1]);
  EXPECT_EQ(route2, captured_routes[2]);
  EXPECT_EQ(route2, captured_routes[3]);
}

}  // namespace patchpanel

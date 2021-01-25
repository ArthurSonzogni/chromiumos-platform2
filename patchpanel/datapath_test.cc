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
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/mock_firewall.h"
#include "patchpanel/net_util.h"

using testing::_;
using testing::ElementsAre;
using testing::ElementsAreArray;
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

std::vector<std::string> SplitCommand(const std::string& command) {
  return base::SplitString(command, " ",
                           base::WhitespaceHandling::TRIM_WHITESPACE,
                           base::SplitResult::SPLIT_WANT_NONEMPTY);
}

}  // namespace

using IpFamily::Dual;
using IpFamily::IPv4;
using IpFamily::IPv6;

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner() = default;
  ~MockProcessRunner() = default;

  MOCK_METHOD3(brctl,
               int(const std::string& cmd,
                   const std::vector<std::string>& argv,
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
  MOCK_METHOD4(iptables,
               int(const std::string& table,
                   const std::vector<std::string>& argv,
                   bool log_failures,
                   std::string* output));
  MOCK_METHOD4(ip6tables,
               int(const std::string& table,
                   const std::vector<std::string>& argv,
                   bool log_failures,
                   std::string* output));
  MOCK_METHOD3(sysctl_w,
               int(const std::string& key,
                   const std::string& value,
                   bool log_failures));
  MOCK_METHOD3(ip_netns_attach,
               int(const std::string& netns_name,
                   pid_t netns_pid,
                   bool log_failures));
  MOCK_METHOD2(ip_netns_delete,
               int(const std::string& netns_name, bool log_failures));
};

void Verify_brctl(MockProcessRunner& runner, const std::string& command) {
  auto args = SplitCommand(command);
  const auto action = args[0];
  args.erase(args.begin());
  EXPECT_CALL(runner, brctl(StrEq(action), ElementsAreArray(args), _));
}

void Verify_ip(MockProcessRunner& runner, const std::string& command) {
  auto args = SplitCommand(command);
  const auto object = args[0];
  const auto action = args[1];
  args.erase(args.begin());
  args.erase(args.begin());
  EXPECT_CALL(runner,
              ip(StrEq(object), StrEq(action), ElementsAreArray(args), _));
}

void Verify_ip6(MockProcessRunner& runner, const std::string& command) {
  auto args = SplitCommand(command);
  const auto object = args[0];
  const auto action = args[1];
  args.erase(args.begin());
  args.erase(args.begin());
  EXPECT_CALL(runner,
              ip6(StrEq(object), StrEq(action), ElementsAreArray(args), _));
}

void Verify_iptables(MockProcessRunner& runner,
                     IpFamily family,
                     const std::string& command) {
  auto args =
      base::SplitString(command, " ", base::WhitespaceHandling::TRIM_WHITESPACE,
                        base::SplitResult::SPLIT_WANT_NONEMPTY);
  const auto table = args[0];
  args.erase(args.begin());
  if (family & IPv4)
    EXPECT_CALL(runner,
                iptables(StrEq(table), ElementsAreArray(args), _, nullptr));
  if (family & IPv6)
    EXPECT_CALL(runner,
                ip6tables(StrEq(table), ElementsAreArray(args), _, nullptr));
}

void Verify_sysctl_w(MockProcessRunner& runner,
                     const std::string& key,
                     const std::string& value) {
  EXPECT_CALL(runner, sysctl_w(StrEq(key), StrEq(value), _));
}

void Verify_ip_netns_attach(MockProcessRunner& runner,
                            const std::string& netns_name,
                            pid_t pid) {
  EXPECT_CALL(runner, ip_netns_attach(StrEq(netns_name), pid, _));
}

void Verify_ip_netns_delete(MockProcessRunner& runner,
                            const std::string& netns_name) {
  EXPECT_CALL(runner, ip_netns_delete(StrEq(netns_name), _));
}

TEST(DatapathTest, IpFamily) {
  EXPECT_EQ(Dual, IPv4 | IPv6);
  EXPECT_EQ(Dual & IPv4, IPv4);
  EXPECT_EQ(Dual & IPv6, IPv6);
  EXPECT_NE(Dual, IPv4);
  EXPECT_NE(Dual, IPv6);
  EXPECT_NE(IPv4, IPv6);
}

TEST(DatapathTest, Start) {
  MockProcessRunner runner;
  MockFirewall firewall;

  // Asserts for sysctl modifications
  Verify_sysctl_w(runner, "net.ipv4.ip_forward", "1");
  Verify_sysctl_w(runner, "net.ipv4.ip_local_port_range", "32768 47103");
  Verify_sysctl_w(runner, "net.ipv6.conf.all.forwarding", "1");

  std::vector<std::pair<IpFamily, std::string>> iptables_commands = {
      // Asserts for iptables chain reset.
      {IPv4, "filter -D OUTPUT -j drop_guest_ipv4_prefix -w"},
      {Dual, "filter -F FORWARD -w"},
      {Dual, "mangle -F FORWARD -w"},
      {Dual, "mangle -F INPUT -w"},
      {Dual, "mangle -F OUTPUT -w"},
      {Dual, "mangle -F POSTROUTING -w"},
      {Dual, "mangle -F PREROUTING -w"},
      {Dual, "mangle -L apply_local_source_mark -w"},
      {Dual, "mangle -F apply_local_source_mark -w"},
      {Dual, "mangle -X apply_local_source_mark -w"},
      {Dual, "mangle -L apply_vpn_mark -w"},
      {Dual, "mangle -F apply_vpn_mark -w"},
      {Dual, "mangle -X apply_vpn_mark -w"},
      {Dual, "mangle -L check_routing_mark -w"},
      {Dual, "mangle -F check_routing_mark -w"},
      {Dual, "mangle -X check_routing_mark -w"},
      {IPv4, "filter -L drop_guest_ipv4_prefix -w"},
      {IPv4, "filter -F drop_guest_ipv4_prefix -w"},
      {IPv4, "filter -X drop_guest_ipv4_prefix -w"},
      {IPv4, "nat -F POSTROUTING -w"},
      // Asserts for SNAT rules.
      {IPv4,
       "filter -A FORWARD -m mark --mark 1/1 -m state --state INVALID -j DROP "
       "-w"},
      {IPv4, "nat -A POSTROUTING -m mark --mark 1/1 -j MASQUERADE -w"},
      // Asserts for AddForwardEstablishedRule
      {IPv4,
       "filter -A FORWARD -m state --state ESTABLISHED,RELATED -j ACCEPT -w"},
      // Asserts for AddSourceIPv4DropRule() calls.
      {IPv4, "filter -N drop_guest_ipv4_prefix -w"},
      {IPv4, "filter -I OUTPUT -j drop_guest_ipv4_prefix -w"},
      {IPv4,
       "filter -I drop_guest_ipv4_prefix -o eth+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IPv4,
       "filter -I drop_guest_ipv4_prefix -o wlan+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IPv4,
       "filter -I drop_guest_ipv4_prefix -o mlan+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IPv4,
       "filter -I drop_guest_ipv4_prefix -o usb+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IPv4,
       "filter -I drop_guest_ipv4_prefix -o wwan+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IPv4,
       "filter -I drop_guest_ipv4_prefix -o rmnet+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      // Asserts for AddOutboundIPv4SNATMark("vmtap+")
      {IPv4, "mangle -A PREROUTING -i vmtap+ -j MARK --set-mark 1/1 -w"},
      // Asserts for OUTPUT ndp connmark bypass rule
      {IPv6,
       "mangle -I OUTPUT -p icmpv6 --icmpv6-type router-solicitation -j ACCEPT "
       "-w"},
      {IPv6,
       "mangle -I OUTPUT -p icmpv6 --icmpv6-type router-advertisement -j "
       "ACCEPT -w"},
      {IPv6,
       "mangle -I OUTPUT -p icmpv6 --icmpv6-type neighbour-solicitation -j "
       "ACCEPT -w"},
      {IPv6,
       "mangle -I OUTPUT -p icmpv6 --icmpv6-type neighbour-advertisement -j "
       "ACCEPT -w"},
      {IPv6,
       "mangle -I check_routing_mark -p icmpv6 --icmpv6-type "
       "router-solicitation -j RETURN -w"},
      {IPv6,
       "mangle -I check_routing_mark -p icmpv6 --icmpv6-type "
       "router-advertisement -j RETURN -w"},
      {IPv6,
       "mangle -I check_routing_mark -p icmpv6 --icmpv6-type "
       "neighbour-solicitation -j RETURN -w"},
      {IPv6,
       "mangle -I check_routing_mark -p icmpv6 --icmpv6-type "
       "neighbour-advertisement -j RETURN -w"},
      // Asserts for OUTPUT CONNMARK restore rule
      {Dual,
       "mangle -A OUTPUT -j CONNMARK --restore-mark --mask 0xffff0000 -w"},
      // Asserts for apply_local_source_mark chain
      {Dual, "mangle -N apply_local_source_mark -w"},
      {Dual, "mangle -A OUTPUT -j apply_local_source_mark -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner chronos -j MARK "
       "--set-mark 0x00008100/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner debugd -j MARK "
       "--set-mark 0x00008200/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner cups -j MARK "
       "--set-mark 0x00008200/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner lpadmin -j MARK "
       "--set-mark 0x00008200/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner kerberosd -j "
       "MARK --set-mark 0x00008400/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner kerberosd-exec "
       "-j MARK --set-mark 0x00008400/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner tlsdate -j MARK "
       "--set-mark 0x00008400/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner pluginvm -j "
       "MARK --set-mark 0x00008200/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m owner --uid-owner fuse-smbfs -j "
       "MARK --set-mark 0x00008400/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m cgroup --cgroup 0x00010001 -j "
       "MARK --set-mark 0x00000300/0x0000ff00 -w"},
      {Dual,
       "mangle -A apply_local_source_mark -m mark --mark 0x0/0x00003f00 -j "
       "MARK --set-mark 0x00000400/0x00003f00 -w"},
      // Asserts for apply_vpn_mark chain
      {Dual, "mangle -N apply_vpn_mark -w"},
      {Dual,
       "mangle -A OUTPUT -m mark --mark 0x00008000/0x0000c000 -j "
       "apply_vpn_mark -w"},
      {Dual,
       "mangle -A apply_vpn_mark -m mark ! --mark 0x0/0xffff0000 -j ACCEPT -w"},
      // Asserts for check_routing_mark chain
      {Dual, "mangle -N check_routing_mark -w"},
      {Dual,
       "mangle -A POSTROUTING -j CONNMARK --restore-mark --mask 0xffff0000 -w"},
      {Dual,
       "mangle -A POSTROUTING -m mark ! --mark 0x0/0xffff0000 -j "
       "check_routing_mark -w"},
  };
  for (const auto& c : iptables_commands) {
    Verify_iptables(runner, c.first, c.second);
  }

  Datapath datapath(&runner, &firewall);
  datapath.Start();
}

TEST(DatapathTest, Stop) {
  MockProcessRunner runner;
  MockFirewall firewall;
  // Asserts for sysctl modifications
  Verify_sysctl_w(runner, "net.ipv4.ip_local_port_range", "32768 61000");
  Verify_sysctl_w(runner, "net.ipv6.conf.all.forwarding", "0");
  Verify_sysctl_w(runner, "net.ipv4.ip_forward", "0");
  // Asserts for iptables chain reset.
  std::vector<std::pair<IpFamily, std::string>> iptables_commands = {
      {IPv4, "filter -D OUTPUT -j drop_guest_ipv4_prefix -w"},
      {Dual, "filter -F FORWARD -w"},
      {Dual, "mangle -F FORWARD -w"},
      {Dual, "mangle -F INPUT -w"},
      {Dual, "mangle -F OUTPUT -w"},
      {Dual, "mangle -F POSTROUTING -w"},
      {Dual, "mangle -F PREROUTING -w"},
      {Dual, "mangle -L apply_local_source_mark -w"},
      {Dual, "mangle -F apply_local_source_mark -w"},
      {Dual, "mangle -X apply_local_source_mark -w"},
      {Dual, "mangle -L apply_vpn_mark -w"},
      {Dual, "mangle -F apply_vpn_mark -w"},
      {Dual, "mangle -X apply_vpn_mark -w"},
      {Dual, "mangle -L check_routing_mark -w"},
      {Dual, "mangle -F check_routing_mark -w"},
      {Dual, "mangle -X check_routing_mark -w"},
      {IPv4, "filter -L drop_guest_ipv4_prefix -w"},
      {IPv4, "filter -F drop_guest_ipv4_prefix -w"},
      {IPv4, "filter -X drop_guest_ipv4_prefix -w"},
      {IPv4, "nat -F POSTROUTING -w"},
  };
  for (const auto& c : iptables_commands) {
    Verify_iptables(runner, c.first, c.second);
  }

  Datapath datapath(&runner, &firewall);
  datapath.Stop();
}

TEST(DatapathTest, AddTAP) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Datapath datapath(&runner, &firewall, ioctl_req_cap);
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
  ioctl_rtentry_args.clear();
}

TEST(DatapathTest, AddTAPWithOwner) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Datapath datapath(&runner, &firewall, ioctl_req_cap);
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
  ioctl_rtentry_args.clear();
}

TEST(DatapathTest, AddTAPNoAddrs) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Datapath datapath(&runner, &firewall, ioctl_req_cap);
  auto ifname = datapath.AddTAP("foo0", nullptr, nullptr, "");
  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {TUNSETIFF, TUNSETPERSIST, SIOCGIFFLAGS,
                                       SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
  ioctl_rtentry_args.clear();
}

TEST(DatapathTest, RemoveTAP) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip(runner, "tuntap del foo0 mode tap");
  Datapath datapath(&runner, &firewall);
  datapath.RemoveTAP("foo0");
}

TEST(DatapathTest, NetnsAttachName) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip_netns_delete(runner, "netns_foo");
  Verify_ip_netns_attach(runner, "netns_foo", 1234);
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(datapath.NetnsAttachName("netns_foo", 1234));
}

TEST(DatapathTest, NetnsDeleteName) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, ip_netns_delete(StrEq("netns_foo"), true));
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(datapath.NetnsDeleteName("netns_foo"));
}

TEST(DatapathTest, AddBridge) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Datapath datapath(&runner, &firewall);
  Verify_brctl(runner, "addbr br");
  Verify_ip(runner, "addr add 1.1.1.1/30 brd 1.1.1.3 dev br");
  Verify_ip(runner, "link set br up");
  Verify_iptables(runner, IPv4,
                  "mangle -A PREROUTING -i br -j MARK --set-mark 1/1 -w");
  datapath.AddBridge("br", Ipv4Addr(1, 1, 1, 1), 30);
}

TEST(DatapathTest, ConnectVethPair) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip(runner,
            "link add veth_foo type veth peer name peer_foo netns netns_foo");
  Verify_ip(runner,
            "addr add 100.115.92.169/30 brd 100.115.92.171 dev peer_foo");
  Verify_ip(runner,
            "link set dev peer_foo up addr 01:02:03:04:05:06 multicast on");
  Verify_ip(runner, "link set veth_foo up");
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(datapath.ConnectVethPair(kTestPID, "netns_foo", "veth_foo",
                                       "peer_foo", {1, 2, 3, 4, 5, 6},
                                       Ipv4Addr(100, 115, 92, 169), 30, true));
}

TEST(DatapathTest, AddVirtualInterfacePair) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip(runner,
            "link add veth_foo type veth peer name peer_foo netns netns_foo");
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(
      datapath.AddVirtualInterfacePair("netns_foo", "veth_foo", "peer_foo"));
}

TEST(DatapathTest, ToggleInterface) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip(runner, "link set foo up");
  Verify_ip(runner, "link set bar down");
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(datapath.ToggleInterface("foo", true));
  EXPECT_TRUE(datapath.ToggleInterface("bar", false));
}

TEST(DatapathTest, ConfigureInterface) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip(runner, "addr add 1.1.1.1/30 brd 1.1.1.3 dev foo");
  Verify_ip(runner, "link set dev foo up addr 02:02:02:02:02:02 multicast on");

  Datapath datapath(&runner, &firewall);
  MacAddress mac_addr = {2, 2, 2, 2, 2, 2};
  EXPECT_TRUE(datapath.ConfigureInterface("foo", mac_addr, Ipv4Addr(1, 1, 1, 1),
                                          30, true, true));
}

TEST(DatapathTest, RemoveInterface) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip(runner, "link delete foo");
  Datapath datapath(&runner, &firewall);
  datapath.RemoveInterface("foo");
}

TEST(DatapathTest, RemoveBridge) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(runner, IPv4,
                  "mangle -D PREROUTING -i br -j MARK --set-mark 1/1 -w");
  Verify_ip(runner, "link set br down");
  Verify_brctl(runner, "delbr br");
  Datapath datapath(&runner, &firewall);
  datapath.RemoveBridge("br");
}

TEST(DatapathTest, AddRemoveSourceIPv4DropRule) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(
      runner, IPv4,
      "filter -I drop_guest_ipv4_prefix -o eth+ -s 100.115.92.0/24 -j DROP -w");
  Verify_iptables(
      runner, IPv4,
      "filter -D drop_guest_ipv4_prefix -o eth+ -s 100.115.92.0/24 -j DROP -w");
  Datapath datapath(&runner, &firewall);
  datapath.AddSourceIPv4DropRule("eth+", "100.115.92.0/24");
  datapath.RemoveSourceIPv4DropRule("eth+", "100.115.92.0/24");
}

TEST(DatapathTest, StartRoutingNamespace) {
  MockProcessRunner runner;
  MockFirewall firewall;
  MacAddress mac = {1, 2, 3, 4, 5, 6};

  Verify_ip_netns_delete(runner, "netns_foo");
  Verify_ip_netns_attach(runner, "netns_foo", kTestPID);
  Verify_ip(runner,
            "link add arc_ns0 type veth peer name veth0 netns netns_foo");
  Verify_ip(runner, "addr add 100.115.92.130/30 brd 100.115.92.131 dev veth0");
  Verify_ip(runner,
            "link set dev veth0 up addr 01:02:03:04:05:06 multicast off");
  Verify_ip(runner, "link set arc_ns0 up");
  Verify_ip(runner,
            "addr add 100.115.92.129/30 brd 100.115.92.131 dev arc_ns0");
  Verify_ip(runner,
            "link set dev arc_ns0 up addr 01:02:03:04:05:06 multicast off");
  Verify_iptables(runner, IPv4, "filter -A FORWARD -o arc_ns0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4, "filter -A FORWARD -i arc_ns0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4,
                  "mangle -A PREROUTING -i arc_ns0 -j MARK --set-mark 1/1 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arc_ns0 -j MARK --set-mark "
                  "0x00000200/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arc_ns0 -j CONNMARK "
                  "--restore-mark --mask 0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arc_ns0 -j apply_vpn_mark -w");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = kTestPID;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::USER;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_subnet = std::make_unique<Subnet>(Ipv4Addr(100, 115, 92, 128), 30,
                                                base::DoNothing());
  nsinfo.peer_mac_addr = mac;
  Datapath datapath(&runner, &firewall, (ioctl_t)ioctl_rtentry_cap);
  datapath.StartRoutingNamespace(nsinfo);
  ioctl_reqs.clear();
  ioctl_rtentry_args.clear();
}

TEST(DatapathTest, StopRoutingNamespace) {
  MockProcessRunner runner;
  MockFirewall firewall;

  Verify_iptables(runner, IPv4, "filter -D FORWARD -o arc_ns0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4, "filter -D FORWARD -i arc_ns0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4,
                  "mangle -D PREROUTING -i arc_ns0 -j MARK --set-mark 1/1 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arc_ns0 -j MARK --set-mark "
                  "0x00000200/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arc_ns0 -j CONNMARK "
                  "--restore-mark --mask 0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arc_ns0 -j apply_vpn_mark -w");
  Verify_ip_netns_delete(runner, "netns_foo");
  Verify_ip(runner, "link delete arc_ns0");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = kTestPID;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::USER;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_subnet = std::make_unique<Subnet>(Ipv4Addr(100, 115, 92, 128), 30,
                                                base::DoNothing());
  Datapath datapath(&runner, &firewall);
  datapath.StopRoutingNamespace(nsinfo);
}

TEST(DatapathTest, StartRoutingDevice_Arc) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(
      runner, IPv4,
      "nat -A PREROUTING -i eth0 -m socket --nowildcard -j ACCEPT -w");
  Verify_iptables(
      runner, IPv4,
      "nat -A PREROUTING -i eth0 -p tcp -j DNAT --to-destination 1.2.3.4 -w");
  Verify_iptables(
      runner, IPv4,
      "nat -A PREROUTING -i eth0 -p udp -j DNAT --to-destination 1.2.3.4 -w");
  Verify_iptables(runner, IPv4,
                  "filter -A FORWARD -i eth0 -o arc_eth0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4,
                  "filter -A FORWARD -i arc_eth0 -o eth0 -j ACCEPT -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arc_eth0 -j MARK --set-mark "
                  "0x00002000/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arc_eth0 -j MARK --set-mark "
                  "0x03ea0000/0xffff0000 -w");

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("eth0", 2);
  datapath.StartRoutingDevice("eth0", "arc_eth0", Ipv4Addr(1, 2, 3, 4),
                              TrafficSource::ARC, false);
}

TEST(DatapathTest, StartRoutingDevice_CrosVM) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(runner, IPv4, "filter -A FORWARD -o vmtap0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4, "filter -A FORWARD -i vmtap0 -j ACCEPT -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i vmtap0 -j MARK --set-mark "
                  "0x00002100/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i vmtap0 -j CONNMARK "
                  "--restore-mark --mask 0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i vmtap0 -j apply_vpn_mark -w");

  Datapath datapath(&runner, &firewall);
  datapath.StartRoutingDevice("", "vmtap0", Ipv4Addr(1, 2, 3, 4),
                              TrafficSource::CROSVM, true);
}

TEST(DatapathTest, StopRoutingDevice_Arc) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(
      runner, IPv4,
      "nat -D PREROUTING -i eth0 -m socket --nowildcard -j ACCEPT -w");
  Verify_iptables(
      runner, IPv4,
      "nat -D PREROUTING -i eth0 -p tcp -j DNAT --to-destination 1.2.3.4 -w");
  Verify_iptables(
      runner, IPv4,
      "nat -D PREROUTING -i eth0 -p udp -j DNAT --to-destination 1.2.3.4 -w");
  Verify_iptables(runner, IPv4,
                  "filter -D FORWARD -i eth0 -o arc_eth0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4,
                  "filter -D FORWARD -i arc_eth0 -o eth0 -j ACCEPT -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arc_eth0 -j MARK --set-mark "
                  "0x00002000/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arc_eth0 -j MARK --set-mark "
                  "0x03ea0000/0xffff0000 -w");

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("eth0", 2);
  datapath.StopRoutingDevice("eth0", "arc_eth0", Ipv4Addr(1, 2, 3, 4),
                             TrafficSource::ARC, true);
}

TEST(DatapathTest, StopRoutingDevice_CrosVM) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(runner, IPv4, "filter -D FORWARD -o vmtap0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4, "filter -D FORWARD -i vmtap0 -j ACCEPT -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i vmtap0 -j MARK --set-mark "
                  "0x00002100/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i vmtap0 -j CONNMARK "
                  "--restore-mark --mask 0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i vmtap0 -j apply_vpn_mark -w");

  Datapath datapath(&runner, &firewall);
  datapath.StopRoutingDevice("", "vmtap0", Ipv4Addr(1, 2, 3, 4),
                             TrafficSource::CROSVM, true);
}

TEST(DatapathTest, StartStopIpForwarding) {
  struct {
    IpFamily family;
    std::string iif;
    std::string oif;
    std::string start_args;
    std::string stop_args;
    bool result;
  } testcases[] = {
      {IPv4, "", "", {}, {}, false},
      {NONE, "foo", "bar", {}, {}, false},
      {IPv4, "foo", "bar", "filter -A FORWARD -i foo -o bar -j ACCEPT -w",
       "filter -D FORWARD -i foo -o bar -j ACCEPT -w", true},
      {IPv4, "", "bar", "filter -A FORWARD -o bar -j ACCEPT -w",
       "filter -D FORWARD -o bar -j ACCEPT -w", true},
      {IPv4, "foo", "", "filter -A FORWARD -i foo -j ACCEPT -w",
       "filter -D FORWARD -i foo -j ACCEPT -w", true},
      {IPv6, "foo", "bar", "filter -A FORWARD -i foo -o bar -j ACCEPT -w",
       "filter -D FORWARD -i foo -o bar -j ACCEPT -w", true},
      {IPv6, "", "bar", "filter -A FORWARD -o bar -j ACCEPT -w",
       "filter -D FORWARD -o bar -j ACCEPT -w", true},
      {IPv6, "foo", "", "filter -A FORWARD -i foo -j ACCEPT -w",
       "filter -D FORWARD -i foo -j ACCEPT -w", true},
      {Dual, "foo", "bar", "filter -A FORWARD -i foo -o bar -j ACCEPT -w",
       "filter -D FORWARD -i foo -o bar -j ACCEPT -w", true},
      {Dual, "", "bar", "filter -A FORWARD -o bar -j ACCEPT -w",
       "filter -D FORWARD -o bar -j ACCEPT -w", true},
      {Dual, "foo", "", "filter -A FORWARD -i foo -j ACCEPT -w",
       "filter -D FORWARD -i foo -j ACCEPT -w", true},
  };

  for (const auto& tt : testcases) {
    MockProcessRunner runner;
    MockFirewall firewall;
    if (tt.result) {
      Verify_iptables(runner, tt.family, tt.start_args);
      Verify_iptables(runner, tt.family, tt.stop_args);
    }
    Datapath datapath(&runner, &firewall);

    EXPECT_EQ(tt.result, datapath.StartIpForwarding(tt.family, tt.iif, tt.oif));
    EXPECT_EQ(tt.result, datapath.StopIpForwarding(tt.family, tt.iif, tt.oif));
  }
}

TEST(DatapathTest, StartStopConnectionPinning) {
  MockProcessRunner runner;
  MockFirewall firewall;

  // Setup
  Verify_iptables(runner, Dual,
                  "mangle -A check_routing_mark -o eth0 -m mark ! "
                  "--mark 0x03eb0000/0xffff0000 -j DROP -w");
  Verify_iptables(runner, Dual,
                  "mangle -A POSTROUTING -o eth0 -j CONNMARK --set-mark "
                  "0x03eb0000/0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A POSTROUTING -o eth0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i eth0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");

  // Teardown
  Verify_iptables(runner, Dual,
                  "mangle -D check_routing_mark -o eth0 -m mark ! "
                  "--mark 0x03eb0000/0xffff0000 -j DROP -w");
  Verify_iptables(runner, Dual,
                  "mangle -D POSTROUTING -o eth0 -j CONNMARK --set-mark "
                  "0x03eb0000/0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D POSTROUTING -o eth0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i eth0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("eth0", 3);
  datapath.StartConnectionPinning("eth0");
  datapath.StopConnectionPinning("eth0");
}

TEST(DatapathTest, StartStopVpnRouting_ArcVpn) {
  MockProcessRunner runner;
  MockFirewall firewall;

  // Setup
  Verify_iptables(runner, Dual,
                  "mangle -A check_routing_mark -o arcbr0 -m mark ! "
                  "--mark 0x03ed0000/0xffff0000 -j DROP -w");
  Verify_iptables(runner, Dual,
                  "mangle -A POSTROUTING -o arcbr0 -j CONNMARK "
                  "--set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(
      runner, Dual,
      "mangle -A apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A POSTROUTING -o arcbr0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arcbr0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, IPv4,
                  "nat -A POSTROUTING -o arcbr0 -j MASQUERADE -w");

  // Teardown
  Verify_iptables(runner, Dual,
                  "mangle -D check_routing_mark -o arcbr0 -m mark ! "
                  "--mark 0x03ed0000/0xffff0000 -j DROP -w");
  Verify_iptables(runner, Dual,
                  "mangle -D POSTROUTING -o arcbr0 -j CONNMARK "
                  "--set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(
      runner, Dual,
      "mangle -D apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D POSTROUTING -o arcbr0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arcbr0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, IPv4,
                  "nat -D POSTROUTING -o arcbr0 -j MASQUERADE -w");

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("arcbr0", 5);
  datapath.StartVpnRouting("arcbr0");
  datapath.StopVpnRouting("arcbr0");
}

TEST(DatapathTest, StartStopVpnRouting_HostVpn) {
  MockProcessRunner runner;
  MockFirewall firewall;

  // Setup
  Verify_iptables(runner, Dual,
                  "mangle -A check_routing_mark -o tun0 -m mark ! "
                  "--mark 0x03ed0000/0xffff0000 -j DROP -w");
  Verify_iptables(runner, Dual,
                  "mangle -A POSTROUTING -o tun0 -j CONNMARK --set-mark "
                  "0x03ed0000/0xffff0000 -w");
  Verify_iptables(
      runner, Dual,
      "mangle -A apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A POSTROUTING -o tun0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i tun0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, IPv4, "nat -A POSTROUTING -o tun0 -j MASQUERADE -w");
  // Teardown
  Verify_iptables(runner, Dual,
                  "mangle -D check_routing_mark -o tun0 -m mark ! "
                  "--mark 0x03ed0000/0xffff0000 -j DROP -w");
  Verify_iptables(runner, Dual,
                  "mangle -D POSTROUTING -o tun0 -j CONNMARK --set-mark "
                  "0x03ed0000/0xffff0000 -w");
  Verify_iptables(
      runner, Dual,
      "mangle -D apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D POSTROUTING -o tun0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i tun0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(runner, IPv4, "nat -D POSTROUTING -o tun0 -j MASQUERADE -w");
  // Start tun0 <-> arcbr0 routing
  Verify_iptables(runner, IPv4,
                  "filter -A FORWARD -i tun0 -o arcbr0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4,
                  "filter -A FORWARD -i arcbr0 -o tun0 -j ACCEPT -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arcbr0 -j MARK --set-mark "
                  "0x00002000/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -A PREROUTING -i arcbr0 -j MARK --set-mark "
                  "0x03ed0000/0xffff0000 -w");
  // Stop tun0 <-> arcbr0 routing
  Verify_iptables(runner, IPv4,
                  "filter -D FORWARD -i tun0 -o arcbr0 -j ACCEPT -w");
  Verify_iptables(runner, IPv4,
                  "filter -D FORWARD -i arcbr0 -o tun0 -j ACCEPT -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arcbr0 -j MARK --set-mark "
                  "0x00002000/0x00003f00 -w");
  Verify_iptables(runner, Dual,
                  "mangle -D PREROUTING -i arcbr0 -j MARK --set-mark "
                  "0x03ed0000/0xffff0000 -w");

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("tun0", 5);
  datapath.StartVpnRouting("tun0");
  datapath.StopVpnRouting("tun0");
}

TEST(DatapathTest, AddInboundIPv4DNAT) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(
      runner, IPv4,
      "nat -A PREROUTING -i eth0 -m socket --nowildcard -j ACCEPT -w");
  Verify_iptables(
      runner, IPv4,
      "nat -A PREROUTING -i eth0 -p tcp -j DNAT --to-destination 1.2.3.4 -w");
  Verify_iptables(
      runner, IPv4,
      "nat -A PREROUTING -i eth0 -p udp -j DNAT --to-destination 1.2.3.4 -w");

  Datapath datapath(&runner, &firewall);
  datapath.AddInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, RemoveInboundIPv4DNAT) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(
      runner, IPv4,
      "nat -D PREROUTING -i eth0 -m socket --nowildcard -j ACCEPT -w");
  Verify_iptables(
      runner, IPv4,
      "nat -D PREROUTING -i eth0 -p tcp -j DNAT --to-destination 1.2.3.4 -w");
  Verify_iptables(
      runner, IPv4,
      "nat -D PREROUTING -i eth0 -p udp -j DNAT --to-destination 1.2.3.4 -w");

  Datapath datapath(&runner, &firewall);
  datapath.RemoveInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, MaskInterfaceFlags) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Datapath datapath(&runner, &firewall, ioctl_req_cap);

  bool result = datapath.MaskInterfaceFlags("foo0", IFF_DEBUG);
  EXPECT_TRUE(result);
  std::vector<ioctl_req_t> expected = {SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
  ioctl_rtentry_args.clear();
}

TEST(DatapathTest, AddIPv6Forwarding) {
  MockProcessRunner runner;
  MockFirewall firewall;
  // Return 1 on iptables -C to simulate rule not existing case
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                false, nullptr))
      .WillOnce(Return(1));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-A", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                false, nullptr))
      .WillOnce(Return(1));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-A", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                true, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.AddIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6ForwardingRuleExists) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                false, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                false, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.AddIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, RemoveIPv6Forwarding) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_iptables(runner, IPv6,
                  "filter -D FORWARD -i eth0 -o arc_eth0 -j ACCEPT -w");
  Verify_iptables(runner, IPv6,
                  "filter -D FORWARD -i arc_eth0 -o eth0 -j ACCEPT -w");
  Datapath datapath(&runner, &firewall);
  datapath.RemoveIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6HostRoute) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Verify_ip6(runner, "route replace 2001:da8:e00::1234/128 dev eth0");
  Datapath datapath(&runner, &firewall);
  datapath.AddIPv6HostRoute("eth0", "2001:da8:e00::1234", 128);
}

TEST(DatapathTest, AddIPv4Route) {
  MockProcessRunner runner;
  MockFirewall firewall;
  Datapath datapath(&runner, &firewall, (ioctl_t)ioctl_rtentry_cap);

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
  EXPECT_EQ(route1, captured_routes[0]);
  EXPECT_EQ(route1, captured_routes[1]);
  EXPECT_EQ(route2, captured_routes[2]);
  EXPECT_EQ(route2, captured_routes[3]);
  ioctl_reqs.clear();
  ioctl_rtentry_args.clear();
}

TEST(DatapathTest, ArcVethHostName) {
  EXPECT_EQ("vetheth0", ArcVethHostName("eth0"));
  EXPECT_EQ("vethrmnet0", ArcVethHostName("rmnet0"));
  EXPECT_EQ("vethrmnet_data0", ArcVethHostName("rmnet_data0"));
  EXPECT_EQ("vethifnamsiz_i0", ArcVethHostName("ifnamsiz_ifnam0"));
  auto ifname = ArcVethHostName("exceeds_ifnamesiz_checkanyway");
  EXPECT_EQ("vethexceeds_ify", ifname);
  EXPECT_LT(ifname.length(), IFNAMSIZ);
}

TEST(DatapathTest, ArcBridgeName) {
  EXPECT_EQ("arc_eth0", ArcBridgeName("eth0"));
  EXPECT_EQ("arc_rmnet0", ArcBridgeName("rmnet0"));
  EXPECT_EQ("arc_rmnet_data0", ArcBridgeName("rmnet_data0"));
  EXPECT_EQ("arc_ifnamsiz_i0", ArcBridgeName("ifnamsiz_ifnam0"));
  auto ifname = ArcBridgeName("exceeds_ifnamesiz_checkanyway");
  EXPECT_EQ("arc_exceeds_ify", ifname);
  EXPECT_LT(ifname.length(), IFNAMSIZ);
}

}  // namespace patchpanel

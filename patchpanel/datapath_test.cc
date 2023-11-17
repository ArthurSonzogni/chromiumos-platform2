// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/datapath.h"

#include <linux/if_tun.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/fake_system.h"
#include "patchpanel/firewall.h"
#include "patchpanel/iptables.h"
#include "patchpanel/mock_process_runner.h"
#include "patchpanel/net_util.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

using net_base::IPv4Address;
using net_base::IPv4CIDR;
using net_base::IPv6CIDR;

using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Mock;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::SetArgPointee;
using testing::StrEq;

namespace patchpanel {
namespace {

// TODO(hugobenichi) Centralize this constant definition
constexpr pid_t kTestPID = -2;

std::vector<std::string> SplitArgs(const std::string& args) {
  return base::SplitString(args, " ", base::WhitespaceHandling::TRIM_WHITESPACE,
                           base::SplitResult::SPLIT_WANT_NONEMPTY);
}

class MockFirewall : public Firewall {
 public:
  MockFirewall() = default;
  ~MockFirewall() = default;

  MOCK_METHOD3(AddAcceptRules,
               bool(patchpanel::ModifyPortRuleRequest::Protocol protocol,
                    uint16_t port,
                    const std::string& interface));
  MOCK_METHOD3(DeleteAcceptRules,
               bool(Protocol protocol,
                    uint16_t port,
                    const std::string& interface));
  MOCK_METHOD2(AddLoopbackLockdownRules,
               bool(Protocol protocol, uint16_t port));
  MOCK_METHOD2(DeleteLoopbackLockdownRules,
               bool(Protocol protocol, uint16_t port));
  MOCK_METHOD6(AddIpv4ForwardRule,
               bool(Protocol protocol,
                    const std::optional<net_base::IPv4Address>& input_ip,
                    uint16_t port,
                    const std::string& interface,
                    const net_base::IPv4Address& dst_ip,
                    uint16_t dst_port));
  MOCK_METHOD6(DeleteIpv4ForwardRule,
               bool(Protocol protocol,
                    const std::optional<net_base::IPv4Address>& input_ip,
                    uint16_t port,
                    const std::string& interface,
                    const net_base::IPv4Address& dst_ip,
                    uint16_t dst_port));
};

void Verify_ip(MockProcessRunner& runner, const std::string& args) {
  auto argv = SplitArgs(args);
  const auto object = argv[0];
  const auto action = argv[1];
  argv.erase(argv.begin());
  argv.erase(argv.begin());
  EXPECT_CALL(runner,
              ip(StrEq(object), StrEq(action), ElementsAreArray(argv), _, _));
}

void Verify_ip6(MockProcessRunner& runner, const std::string& args) {
  auto argv = SplitArgs(args);
  const auto object = argv[0];
  const auto action = argv[1];
  argv.erase(argv.begin());
  argv.erase(argv.begin());
  EXPECT_CALL(runner,
              ip6(StrEq(object), StrEq(action), ElementsAreArray(argv), _, _));
}

void Verify_iptables(MockProcessRunner& runner,
                     IpFamily family,
                     const std::string& args,
                     int call_count = 1) {
  auto argv = SplitArgs(args);
  const auto table = Iptables::TableFromName(argv[0]);
  const auto command = Iptables::CommandFromName(argv[1]);
  const auto chain = argv[2];
  argv.erase(argv.begin());
  argv.erase(argv.begin());
  argv.erase(argv.begin());
  ASSERT_TRUE(table.has_value())
      << "incorrect table name in expected args: " << args;
  ASSERT_TRUE(command.has_value())
      << "incorrect command name in expected args: " << args;
  if (family == IpFamily::kIPv4 || family == IpFamily::kDual) {
    EXPECT_CALL(runner, iptables(*table, *command, StrEq(chain),
                                 ElementsAreArray(argv), _, _, nullptr))
        .Times(call_count)
        .WillRepeatedly(Return(0));
  }
  if (family == IpFamily::kIPv6 || family == IpFamily::kDual) {
    EXPECT_CALL(runner, ip6tables(*table, *command, StrEq(chain),
                                  ElementsAreArray(argv), _, _, nullptr))
        .Times(call_count)
        .WillRepeatedly(Return(0));
  }
}

void Verify_iptables_in_sequence(MockProcessRunner& runner,
                                 IpFamily family,
                                 const std::string& args,
                                 const Sequence& sequence) {
  auto argv = SplitArgs(args);
  const auto table = Iptables::TableFromName(argv[0]);
  const auto command = Iptables::CommandFromName(argv[1]);
  const auto chain = argv[2];
  argv.erase(argv.begin());
  argv.erase(argv.begin());
  argv.erase(argv.begin());
  ASSERT_TRUE(table.has_value())
      << "incorrect table name in expected args: " << args;
  ASSERT_TRUE(command.has_value())
      << "incorrect command name in expected args: " << args;
  if (family == IpFamily::kIPv4 || family == IpFamily::kDual) {
    EXPECT_CALL(runner, iptables(*table, *command, StrEq(chain),
                                 ElementsAreArray(argv), _, _, nullptr))
        .InSequence(sequence)
        .WillOnce(Return(0));
  }
  if (family == IpFamily::kIPv6 || family == IpFamily::kDual) {
    EXPECT_CALL(runner, ip6tables(*table, *command, StrEq(chain),
                                  ElementsAreArray(argv), _, _, nullptr))
        .InSequence(sequence)
        .WillOnce(Return(0));
  }
}

void Verify_ip_netns_add(MockProcessRunner& runner,
                         const std::string& netns_name) {
  EXPECT_CALL(runner, ip_netns_add(StrEq(netns_name), _));
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

}  // namespace

class DatapathTest : public testing::Test {
 protected:
  DatapathTest()
      : runner_(new MockProcessRunner()),
        firewall_(new MockFirewall()),
        datapath_(runner_, firewall_, &system_) {}

  ~DatapathTest() = default;

  FakeSystem system_;
  MockProcessRunner* runner_;
  MockFirewall* firewall_;

  Datapath datapath_;
};

TEST_F(DatapathTest, Start) {
  // Asserts for sysctl modifications
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv4Forward, "1", ""));
  EXPECT_CALL(system_,
              SysNetSet(System::SysNet::kIPLocalPortRange, "32768 47103", ""));
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6Forward, "1", ""));
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6ProxyNDP, "1", ""));

  static struct {
    IpFamily family;
    std::string args;
    int call_count = 1;
  } iptables_commands[] = {
      // Asserts for iptables chain reset.
      {IpFamily::kDual, "filter -D INPUT -j ingress_port_firewall -w"},
      {IpFamily::kDual, "filter -D OUTPUT -j egress_port_firewall -w"},
      {IpFamily::kIPv4, "filter -D OUTPUT -j drop_guest_ipv4_prefix -w"},
      {IpFamily::kDual, "filter -D OUTPUT -j vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -F FORWARD -w"},
      {IpFamily::kDual, "mangle -F FORWARD -w"},
      {IpFamily::kDual, "mangle -F INPUT -w"},
      {IpFamily::kDual, "mangle -F OUTPUT -w"},
      {IpFamily::kDual, "mangle -F POSTROUTING -w"},
      {IpFamily::kDual, "mangle -F PREROUTING -w"},
      {IpFamily::kDual,
       "mangle -D OUTPUT -m owner ! --uid-owner chronos -j skip_apply_vpn_mark "
       "-w"},
      {IpFamily::kDual, "mangle -L apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -F apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -X apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -L apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -F apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -X apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -L skip_apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -F skip_apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -X skip_apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -L qos_detect_static -w"},
      {IpFamily::kDual, "mangle -F qos_detect_static -w"},
      {IpFamily::kDual, "mangle -X qos_detect_static -w"},
      {IpFamily::kDual, "mangle -L qos_detect -w"},
      {IpFamily::kDual, "mangle -F qos_detect -w"},
      {IpFamily::kDual, "mangle -X qos_detect -w"},
      {IpFamily::kDual, "mangle -L qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -F qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -X qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -L qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -F qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -X qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -L qos_apply_dscp -w"},
      {IpFamily::kDual, "mangle -F qos_apply_dscp -w"},
      {IpFamily::kDual, "mangle -X qos_apply_dscp -w"},
      {IpFamily::kIPv4, "filter -L drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -F drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -X drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -L drop_guest_invalid_ipv4 -w"},
      {IpFamily::kIPv4, "filter -F drop_guest_invalid_ipv4 -w"},
      {IpFamily::kIPv4, "filter -X drop_guest_invalid_ipv4 -w"},
      {IpFamily::kDual, "filter -L vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -F vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -X vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -L vpn_accept -w"},
      {IpFamily::kDual, "filter -F vpn_accept -w"},
      {IpFamily::kDual, "filter -X vpn_accept -w"},
      {IpFamily::kDual, "filter -L vpn_lockdown -w"},
      {IpFamily::kDual, "filter -F vpn_lockdown -w"},
      {IpFamily::kDual, "filter -X vpn_lockdown -w"},
      {IpFamily::kDual, "filter -L accept_downstream_network -w"},
      {IpFamily::kDual, "filter -F accept_downstream_network -w"},
      {IpFamily::kDual, "filter -X accept_downstream_network -w"},
      {IpFamily::kIPv6, "filter -L enforce_ipv6_src_prefix -w"},
      {IpFamily::kIPv6, "filter -F enforce_ipv6_src_prefix -w"},
      {IpFamily::kIPv6, "filter -X enforce_ipv6_src_prefix -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j ingress_port_forwarding -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j apply_auto_dnat_to_parallels -w"},
      {IpFamily::kDual, "nat -D PREROUTING -j redirect_default_dns -w"},
      {IpFamily::kIPv4, "nat -L redirect_dns -w"},
      {IpFamily::kIPv4, "nat -F redirect_dns -w"},
      {IpFamily::kIPv4, "nat -X redirect_dns -w"},
      {IpFamily::kIPv4, "nat -L apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -F apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -X apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -L apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -F apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -X apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -L apply_auto_dnat_to_parallels -w"},
      {IpFamily::kIPv4, "nat -F apply_auto_dnat_to_parallels -w"},
      {IpFamily::kIPv4, "nat -X apply_auto_dnat_to_parallels -w"},
      {IpFamily::kDual, "nat -L redirect_default_dns -w"},
      {IpFamily::kDual, "nat -F redirect_default_dns -w"},
      {IpFamily::kDual, "nat -X redirect_default_dns -w"},
      {IpFamily::kDual, "nat -L redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -F redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -X redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -L redirect_user_dns -w"},
      {IpFamily::kDual, "nat -F redirect_user_dns -w"},
      {IpFamily::kDual, "nat -X redirect_user_dns -w"},
      {IpFamily::kDual, "nat -L snat_chrome_dns -w"},
      {IpFamily::kDual, "nat -F snat_chrome_dns -w"},
      {IpFamily::kDual, "nat -X snat_chrome_dns -w"},
      {IpFamily::kIPv6, "nat -L snat_user_dns -w"},
      {IpFamily::kIPv6, "nat -F snat_user_dns -w"},
      {IpFamily::kIPv6, "nat -X snat_user_dns -w"},
      {IpFamily::kDual, "nat -F POSTROUTING -w"},
      {IpFamily::kDual, "nat -F OUTPUT -w"},
      // Asserts for SNAT rules of traffic forwarded from downstream interfaces.
      {IpFamily::kIPv4, "filter -N drop_guest_invalid_ipv4 -w"},
      {IpFamily::kIPv4, "filter -I FORWARD -j drop_guest_invalid_ipv4 -w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_invalid_ipv4 -m mark --mark 0x00000001/0x00000001 "
       "-m state "
       "--state INVALID -j DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_invalid_ipv4 -s 100.115.92.0/23 -p tcp "
       "--tcp-flags FIN,PSH "
       "FIN,PSH -o wwan+ -j DROP -w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_invalid_ipv4 -s 100.115.92.0/23 -p tcp "
       "--tcp-flags FIN,PSH "
       "FIN,PSH -o mbimmux+ -j DROP -w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_invalid_ipv4 -s 100.115.92.0/23 -p tcp "
       "--tcp-flags FIN,PSH "
       "FIN,PSH -o qmapmux+ -j DROP -w"},
      {IpFamily::kDual,
       "nat -A POSTROUTING -m mark --mark 0x00000001/0x00000001 -j MASQUERADE "
       "-w"},
      // Asserts for AddForwardEstablishedRule
      {IpFamily::kIPv4,
       "filter -A FORWARD -m state --state ESTABLISHED,RELATED -j ACCEPT -w"},
      // Asserts for AddSourceIPv4DropRule() calls.
      {IpFamily::kIPv4, "filter -N drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -I OUTPUT -j drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o eth+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o wlan+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o mlan+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o usb+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o wwan+ -s 100.115.92.0/23 -j DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o mbimmux+ -s 100.115.92.0/23 -j "
       "DROP "
       "-w"},
      {IpFamily::kIPv4,
       "filter -I drop_guest_ipv4_prefix -o qmapmux+ -s 100.115.92.0/23 -j "
       "DROP "
       "-w"},
      // Asserts for forwarding ICMP6.
      {IpFamily::kIPv6, "filter -A FORWARD -p ipv6-icmp -j ACCEPT -w"},
      // Asserts for OUTPUT CONNMARK restore rule
      {IpFamily::kDual,
       "mangle -A OUTPUT -j CONNMARK --restore-mark --mask 0xffff0000 -w"},
      // Asserts for apply_local_source_mark chain
      {IpFamily::kDual, "mangle -N apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -A OUTPUT -j apply_local_source_mark -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m mark ! --mark 0x0/0x00003f00 -j "
       "RETURN -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner chronos -j MARK "
       "--set-mark 0x00008100/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner debugd -j MARK "
       "--set-mark 0x00008200/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner cups -j MARK "
       "--set-mark 0x00008200/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner lpadmin -j MARK "
       "--set-mark 0x00008200/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner kerberosd -j "
       "MARK --set-mark 0x00008400/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner kerberosd-exec "
       "-j MARK --set-mark 0x00008400/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner tlsdate -j MARK "
       "--set-mark 0x00008400/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner pluginvm -j "
       "MARK --set-mark 0x00008200/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m owner --uid-owner fuse-smbfs -j "
       "MARK --set-mark 0x00008400/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m cgroup --cgroup 0x00010001 -j "
       "MARK --set-mark 0x00000300/0x0000ff00 -w"},
      {IpFamily::kDual,
       "mangle -A apply_local_source_mark -m mark --mark 0x0/0x00003f00 -j "
       "MARK --set-mark 0x00000400/0x00003f00 -w"},
      // Asserts for apply_vpn_mark chain
      {IpFamily::kDual, "mangle -N apply_vpn_mark -w"},
      {IpFamily::kDual,
       "mangle -A OUTPUT -m mark --mark 0x00008000/0x0000c000 -j "
       "apply_vpn_mark -w"},
      // Asserts for redirect_dns chain creation
      {IpFamily::kIPv4, "nat -N redirect_dns -w"},
      // Asserts for VPN filter chain creations
      {IpFamily::kDual, "filter -N vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -I OUTPUT -j vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -I FORWARD -j vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -N vpn_lockdown -w"},
      {IpFamily::kDual, "filter -A vpn_egress_filters -j vpn_lockdown -w"},
      {IpFamily::kDual, "filter -N vpn_accept -w"},
      {IpFamily::kDual, "filter -A vpn_egress_filters -j vpn_accept -w"},
      // Asserts for cellular prefix enforcement chain creation
      {IpFamily::kIPv6, "filter -N enforce_ipv6_src_prefix -w"},
      {IpFamily::kDual, "filter -N drop_forward_to_bruschetta -w"},
      {IpFamily::kDual, "filter -N drop_output_to_bruschetta -w"},
      {IpFamily::kDual, "filter -A FORWARD -j drop_forward_to_bruschetta -w"},
      {IpFamily::kDual, "filter -I OUTPUT -j drop_output_to_bruschetta -w"},
      // Asserts for DNS proxy rules
      {IpFamily::kDual, "mangle -N skip_apply_vpn_mark -w"},
      {IpFamily::kDual,
       "mangle -A OUTPUT -m owner ! --uid-owner chronos -j skip_apply_vpn_mark "
       "-w"},
      {IpFamily::kIPv4, "nat -N apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -N apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -N apply_auto_dnat_to_parallels -w"},
      {IpFamily::kIPv4, "nat -N ingress_port_forwarding -w"},
      {IpFamily::kIPv4, "nat -A PREROUTING -j apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -A PREROUTING -j apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -A PREROUTING -j apply_auto_dnat_to_parallels -w"},
      {IpFamily::kIPv4, "nat -A PREROUTING -j ingress_port_forwarding -w"},
      {IpFamily::kDual, "nat -N redirect_default_dns -w"},
      {IpFamily::kDual, "nat -N redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -N redirect_user_dns -w"},
      {IpFamily::kDual, "nat -A PREROUTING -j redirect_default_dns -w"},
      {IpFamily::kDual, "nat -A OUTPUT -j redirect_chrome_dns -w"},
      {IpFamily::kDual,
       "nat -A OUTPUT -m mark --mark 0x00008000/0x0000c000 -j "
       "redirect_user_dns -w"},
      {IpFamily::kDual, "nat -N snat_chrome_dns -w"},
      {IpFamily::kIPv6, "nat -N snat_user_dns -w"},
      {IpFamily::kDual,
       "nat -A POSTROUTING -m mark --mark 0x00000100/0x00003f00 -j "
       "snat_chrome_dns -w"},
      {IpFamily::kIPv6,
       "nat -A POSTROUTING -m mark --mark 0x00008000/0x0000c000 -j "
       "snat_user_dns -w"},
      // Asserts for egress and ingress port firewall chains
      {IpFamily::kDual, "filter -N ingress_port_firewall -w"},
      {IpFamily::kDual, "filter -A INPUT -j ingress_port_firewall -w"},
      {IpFamily::kDual, "filter -N egress_port_firewall -w"},
      {IpFamily::kDual, "filter -A OUTPUT -j egress_port_firewall -w"},
      {IpFamily::kDual, "filter -N accept_downstream_network -w"},
      // Asserts for QoS detect chains.
      {IpFamily::kDual, "mangle -N qos_detect_static -w"},
      {IpFamily::kDual, "mangle -A OUTPUT -j qos_detect_static -w"},
      {IpFamily::kDual, "mangle -A PREROUTING -j qos_detect_static -w"},
      {IpFamily::kDual, "mangle -N qos_detect -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -m mark ! --mark 0x00000000/0x000000e0 -j MARK "
       "--set-xmark 0x00000000/0x000000e0 -w"},
      {IpFamily::kDual, "mangle -A qos_detect -m dscp ! --dscp 0 -j RETURN -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -j CONNMARK --restore-mark --nfmask 0x000000e0 "
       "--ctmask 0x000000e0 -w"},
      {IpFamily::kDual, "mangle -N qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -A qos_detect -j qos_detect_borealis -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -m mark ! --mark 0x00000000/0x000000e0 -j RETURN "
       "-w",
       /*call_count=*/2},
      {IpFamily::kIPv4,
       "mangle -A qos_detect -p icmp -j MARK --set-xmark 0x00000060/0x000000e0 "
       "-w"},
      {IpFamily::kIPv6,
       "mangle -A qos_detect -p icmpv6 -j MARK --set-xmark "
       "0x00000060/0x000000e0 -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -p tcp --syn -j MARK --set-xmark "
       "0x00000060/0x000000e0 -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -p udp --dport 53 -j MARK --set-xmark "
       "0x00000060/0x000000e0 -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -p tcp --dport 53 -j MARK --set-xmark "
       "0x00000060/0x000000e0 -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -m bpf --object-pinned "
       "/run/patchpanel/bpf/match_dtls_srtp -j MARK --set-xmark "
       "0x00000040/0x000000e0 -w"},
      {IpFamily::kDual,
       "mangle -A qos_detect -j CONNMARK --save-mark --nfmask 0x000000e0 "
       "--ctmask 0x000000e0 -w"},
      {IpFamily::kDual, "mangle -N qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -A qos_detect -j qos_detect_doh -w"},
      // Asserts for QoS apply DSCP chain.
      {IpFamily::kDual, "mangle -N qos_apply_dscp -w"},
      {IpFamily::kDual,
       "mangle -A qos_apply_dscp -m mark --mark 0x00000020/0x000000e0 -j DSCP "
       "--set-dscp 32 -w"},
      {IpFamily::kDual,
       "mangle -A qos_apply_dscp -m mark --mark 0x00000040/0x000000e0 -j DSCP "
       "--set-dscp 34 -w"},
      {IpFamily::kDual,
       "mangle -A qos_apply_dscp -m mark --mark 0x00000060/0x000000e0 -j DSCP "
       "--set-dscp 48 -w"},
      {IpFamily::kDual,
       "mangle -A qos_apply_dscp -m mark --mark 0x00000080/0x000000e0 -j DSCP "
       "--set-dscp 34 -w"},
      {IpFamily::kIPv6,
       "filter -A enforce_ipv6_src_prefix -s 2000::/3 -j DROP -w"},
      {IpFamily::kIPv6,
       "filter -A enforce_ipv6_src_prefix -s fc00::/7 -j DROP -w"},
  };
  for (const auto& c : iptables_commands) {
    Verify_iptables(*runner_, c.family, c.args, c.call_count);
  }

  datapath_.Start();
}

TEST_F(DatapathTest, Stop) {
  // Asserts for sysctl modifications
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv4Forward, "0", ""));
  EXPECT_CALL(system_,
              SysNetSet(System::SysNet::kIPLocalPortRange, "32768 61000", ""));
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6Forward, "0", ""));
  // Asserts for iptables chain reset.
  std::vector<std::pair<IpFamily, std::string>> iptables_commands = {
      {IpFamily::kIPv4, "filter -D OUTPUT -j drop_guest_ipv4_prefix -w"},
      {IpFamily::kDual, "filter -D OUTPUT -j vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -F FORWARD -w"},
      {IpFamily::kDual, "mangle -F FORWARD -w"},
      {IpFamily::kDual, "mangle -F INPUT -w"},
      {IpFamily::kDual, "mangle -F OUTPUT -w"},
      {IpFamily::kDual, "mangle -F POSTROUTING -w"},
      {IpFamily::kDual, "mangle -F PREROUTING -w"},
      {IpFamily::kDual,
       "mangle -D OUTPUT -m owner ! --uid-owner chronos -j skip_apply_vpn_mark "
       "-w"},
      {IpFamily::kDual, "mangle -L apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -F apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -X apply_local_source_mark -w"},
      {IpFamily::kDual, "mangle -L apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -F apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -X apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -L skip_apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -F skip_apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -X skip_apply_vpn_mark -w"},
      {IpFamily::kDual, "mangle -L qos_detect_static -w"},
      {IpFamily::kDual, "mangle -F qos_detect_static -w"},
      {IpFamily::kDual, "mangle -X qos_detect_static -w"},
      {IpFamily::kDual, "mangle -L qos_detect -w"},
      {IpFamily::kDual, "mangle -F qos_detect -w"},
      {IpFamily::kDual, "mangle -X qos_detect -w"},
      {IpFamily::kDual, "mangle -L qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -F qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -X qos_detect_borealis -w"},
      {IpFamily::kDual, "mangle -L qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -F qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -X qos_detect_doh -w"},
      {IpFamily::kDual, "mangle -L qos_apply_dscp -w"},
      {IpFamily::kDual, "mangle -F qos_apply_dscp -w"},
      {IpFamily::kDual, "mangle -X qos_apply_dscp -w"},
      {IpFamily::kIPv4, "filter -L drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -F drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -X drop_guest_ipv4_prefix -w"},
      {IpFamily::kIPv4, "filter -L drop_guest_invalid_ipv4 -w"},
      {IpFamily::kIPv4, "filter -F drop_guest_invalid_ipv4 -w"},
      {IpFamily::kIPv4, "filter -X drop_guest_invalid_ipv4 -w"},
      {IpFamily::kDual, "filter -L vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -F vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -X vpn_egress_filters -w"},
      {IpFamily::kDual, "filter -L vpn_accept -w"},
      {IpFamily::kDual, "filter -F vpn_accept -w"},
      {IpFamily::kDual, "filter -X vpn_accept -w"},
      {IpFamily::kDual, "filter -L vpn_lockdown -w"},
      {IpFamily::kDual, "filter -F vpn_lockdown -w"},
      {IpFamily::kDual, "filter -X vpn_lockdown -w"},
      {IpFamily::kDual, "filter -L accept_downstream_network -w"},
      {IpFamily::kDual, "filter -F accept_downstream_network -w"},
      {IpFamily::kDual, "filter -X accept_downstream_network -w"},
      {IpFamily::kDual, "filter -D INPUT -j ingress_port_firewall -w"},
      {IpFamily::kDual, "filter -D OUTPUT -j egress_port_firewall -w"},
      {IpFamily::kIPv6, "filter -L enforce_ipv6_src_prefix -w"},
      {IpFamily::kIPv6, "filter -F enforce_ipv6_src_prefix -w"},
      {IpFamily::kIPv6, "filter -X enforce_ipv6_src_prefix -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j ingress_port_forwarding -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -D PREROUTING -j apply_auto_dnat_to_parallels -w"},
      {IpFamily::kDual, "nat -D PREROUTING -j redirect_default_dns -w"},
      {IpFamily::kIPv4, "nat -L redirect_dns -w"},
      {IpFamily::kIPv4, "nat -F redirect_dns -w"},
      {IpFamily::kIPv4, "nat -X redirect_dns -w"},
      {IpFamily::kIPv4, "nat -L apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -F apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -X apply_auto_dnat_to_arc -w"},
      {IpFamily::kIPv4, "nat -L apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -F apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -X apply_auto_dnat_to_crostini -w"},
      {IpFamily::kIPv4, "nat -L apply_auto_dnat_to_parallels -w"},
      {IpFamily::kIPv4, "nat -F apply_auto_dnat_to_parallels -w"},
      {IpFamily::kIPv4, "nat -X apply_auto_dnat_to_parallels -w"},
      {IpFamily::kDual, "nat -L redirect_default_dns -w"},
      {IpFamily::kDual, "nat -F redirect_default_dns -w"},
      {IpFamily::kDual, "nat -X redirect_default_dns -w"},
      {IpFamily::kDual, "nat -L redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -F redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -X redirect_chrome_dns -w"},
      {IpFamily::kDual, "nat -L redirect_user_dns -w"},
      {IpFamily::kDual, "nat -F redirect_user_dns -w"},
      {IpFamily::kDual, "nat -X redirect_user_dns -w"},
      {IpFamily::kDual, "nat -L snat_chrome_dns -w"},
      {IpFamily::kDual, "nat -F snat_chrome_dns -w"},
      {IpFamily::kDual, "nat -X snat_chrome_dns -w"},
      {IpFamily::kIPv6, "nat -L snat_user_dns -w"},
      {IpFamily::kIPv6, "nat -F snat_user_dns -w"},
      {IpFamily::kIPv6, "nat -X snat_user_dns -w"},
      {IpFamily::kDual, "nat -F POSTROUTING -w"},
      {IpFamily::kDual, "nat -F OUTPUT -w"},
  };
  for (const auto& c : iptables_commands) {
    Verify_iptables(*runner_, c.first, c.second);
  }

  datapath_.Stop();
}

TEST_F(DatapathTest, AddTUN) {
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  auto ifname = datapath_.AddTunTap("foo0", mac, subnet.CIDRAtOffset(1), "",
                                    DeviceMode::kTun);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,     TUNSETPERSIST, SIOCSIFADDR, SIOCSIFNETMASK,
      SIOCSIFHWADDR, SIOCGIFFLAGS,  SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddTunWithoutMACAddress) {
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  auto ifname = datapath_.AddTunTap(
      "foo0", std::nullopt, subnet.CIDRAtOffset(1), "", DeviceMode::kTun);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {TUNSETIFF,    TUNSETPERSIST,
                                       SIOCSIFADDR,  SIOCSIFNETMASK,
                                       SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, RemoveTUN) {
  Verify_ip(*runner_, "tuntap del foo0 mode tun");

  datapath_.RemoveTunTap("foo0", DeviceMode::kTun);
}

TEST_F(DatapathTest, AddTAP) {
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  auto ifname = datapath_.AddTunTap("foo0", mac, subnet.CIDRAtOffset(1), "",
                                    DeviceMode::kTap);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,     TUNSETPERSIST, SIOCSIFADDR, SIOCSIFNETMASK,
      SIOCSIFHWADDR, SIOCGIFFLAGS,  SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddTAPWithOwner) {
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  auto ifname = datapath_.AddTunTap("foo0", mac, subnet.CIDRAtOffset(1), "root",
                                    DeviceMode::kTap);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,      TUNSETPERSIST, TUNSETOWNER,  SIOCSIFADDR,
      SIOCSIFNETMASK, SIOCSIFHWADDR, SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddTAPNoAddrs) {
  auto ifname = datapath_.AddTunTap("foo0", std::nullopt, std::nullopt, "",
                                    DeviceMode::kTap);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {TUNSETIFF, TUNSETPERSIST, SIOCGIFFLAGS,
                                       SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, RemoveTAP) {
  Verify_ip(*runner_, "tuntap del foo0 mode tap");

  datapath_.RemoveTunTap("foo0", DeviceMode::kTap);
}

TEST_F(DatapathTest, NetnsAttachName) {
  Verify_ip_netns_delete(*runner_, "netns_foo");
  Verify_ip_netns_attach(*runner_, "netns_foo", 1234);

  EXPECT_TRUE(datapath_.NetnsAttachName("netns_foo", 1234));
}

TEST_F(DatapathTest, NetnsDeleteName) {
  EXPECT_CALL(*runner_, ip_netns_delete(StrEq("netns_foo"), true));

  EXPECT_TRUE(datapath_.NetnsDeleteName("netns_foo"));
}

TEST_F(DatapathTest, AddBridge) {
  Verify_ip(*runner_, "addr add 1.1.1.1/30 brd 1.1.1.3 dev br");
  Verify_ip(*runner_, "link set br up");

  datapath_.AddBridge("br", *IPv4CIDR::CreateFromCIDRString("1.1.1.1/30"));

  EXPECT_EQ(1, system_.ioctl_reqs.size());
  EXPECT_EQ(SIOCBRADDBR, system_.ioctl_reqs[0]);
  EXPECT_EQ("br", system_.ioctl_ifreq_args[0].first);
}

TEST_F(DatapathTest, RemoveBridge) {
  Verify_ip(*runner_, "link set br down");

  datapath_.RemoveBridge("br");

  EXPECT_EQ(1, system_.ioctl_reqs.size());
  EXPECT_EQ(SIOCBRDELBR, system_.ioctl_reqs[0]);
  EXPECT_EQ("br", system_.ioctl_ifreq_args[0].first);
}

TEST_F(DatapathTest, AddToBridge) {
  EXPECT_CALL(system_, IfNametoindex("vethwlan0")).WillRepeatedly(Return(5));

  datapath_.AddToBridge("arcbr0", "vethwlan0");

  EXPECT_EQ(1, system_.ioctl_reqs.size());
  EXPECT_EQ(SIOCBRADDIF, system_.ioctl_reqs[0]);
  EXPECT_EQ("arcbr0", system_.ioctl_ifreq_args[0].first);
  EXPECT_EQ(5, system_.ioctl_ifreq_args[0].second.ifr_ifindex);
}

TEST_F(DatapathTest, ConnectVethPair) {
  Verify_ip(*runner_,
            "link add veth_foo type veth peer name peer_foo netns netns_foo");
  Verify_ip(*runner_,
            "addr add 100.115.92.169/30 brd 100.115.92.171 dev peer_foo");
  Verify_ip(*runner_,
            "link set dev peer_foo up addr 01:02:03:04:05:06 multicast on");
  Verify_ip(*runner_, "link set veth_foo up");

  EXPECT_TRUE(datapath_.ConnectVethPair(
      kTestPID, "netns_foo", "veth_foo", "peer_foo", {1, 2, 3, 4, 5, 6},
      *IPv4CIDR::CreateFromCIDRString("100.115.92.169/30"),
      /*remote_ipv6_cidr=*/std::nullopt, true));
}

TEST_F(DatapathTest, ConnectVethPair_StaticIPv6) {
  Verify_ip(*runner_,
            "link add veth_foo type veth peer name peer_foo netns netns_foo");
  Verify_ip(*runner_,
            "addr add 100.115.92.169/30 brd 100.115.92.171 dev peer_foo");
  Verify_ip(*runner_, "addr add fd11::1234/64 dev peer_foo");
  Verify_ip(*runner_,
            "link set dev peer_foo up addr 01:02:03:04:05:06 multicast on");
  Verify_ip(*runner_, "link set veth_foo up");

  EXPECT_TRUE(datapath_.ConnectVethPair(
      kTestPID, "netns_foo", "veth_foo", "peer_foo", {1, 2, 3, 4, 5, 6},
      *IPv4CIDR::CreateFromCIDRString("100.115.92.169/30"),
      *IPv6CIDR::CreateFromCIDRString("fd11::1234/64"), true));
}

TEST_F(DatapathTest, AddVirtualInterfacePair) {
  Verify_ip(*runner_,
            "link add veth_foo type veth peer name peer_foo netns netns_foo");

  EXPECT_TRUE(
      datapath_.AddVirtualInterfacePair("netns_foo", "veth_foo", "peer_foo"));
}

TEST_F(DatapathTest, ToggleInterface) {
  Verify_ip(*runner_, "link set foo up");
  Verify_ip(*runner_, "link set bar down");

  EXPECT_TRUE(datapath_.ToggleInterface("foo", true));
  EXPECT_TRUE(datapath_.ToggleInterface("bar", false));
}

TEST_F(DatapathTest, ConfigureInterface) {
  Verify_ip(*runner_, "addr add 100.115.92.2/30 brd 100.115.92.3 dev test0");
  Verify_ip(*runner_,
            "link set dev test0 up addr 02:02:02:02:02:03 multicast on");
  MacAddress mac_addr = {2, 2, 2, 2, 2, 3};
  EXPECT_TRUE(datapath_.ConfigureInterface(
      "test0", mac_addr, *IPv4CIDR::CreateFromCIDRString("100.115.92.2/30"),
      /*ipv6_cidr=*/std::nullopt, true, true));
  Mock::VerifyAndClearExpectations(runner_);

  Verify_ip(*runner_, "addr add 192.168.1.37/24 brd 192.168.1.255 dev test1");
  Verify_ip(*runner_, "link set dev test1 up multicast off");
  EXPECT_TRUE(datapath_.ConfigureInterface(
      "test1", std::nullopt, *IPv4CIDR::CreateFromCIDRString("192.168.1.37/24"),
      /*ipv6_cidr=*/std::nullopt, true, false));
}

TEST_F(DatapathTest, RemoveInterface) {
  Verify_ip(*runner_, "link delete foo");

  datapath_.RemoveInterface("foo");
}

TEST_F(DatapathTest, StartRoutingNamespace) {
  MacAddress peer_mac = {1, 2, 3, 4, 5, 6};
  MacAddress host_mac = {6, 5, 4, 3, 2, 1};

  Verify_ip_netns_delete(*runner_, "netns_foo");
  Verify_ip_netns_attach(*runner_, "netns_foo", kTestPID);
  Verify_ip(*runner_,
            "link add arc_ns0 type veth peer name veth0 netns netns_foo");
  Verify_ip(*runner_,
            "addr add 100.115.92.130/30 brd 100.115.92.131 dev veth0");
  Verify_ip(*runner_,
            "link set dev veth0 up addr 01:02:03:04:05:06 multicast off");
  Verify_ip(*runner_, "link set arc_ns0 up");
  Verify_ip(*runner_,
            "addr add 100.115.92.129/30 brd 100.115.92.131 dev arc_ns0");
  Verify_ip(*runner_,
            "link set dev arc_ns0 up addr 06:05:04:03:02:01 multicast off");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -o arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -i arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                  "0x00000001/0x00000001 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                  "0x00000200/0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j CONNMARK "
                  "--restore-mark --mask 0xffff0000 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_arc_ns0 -s 100.115.92.130 -d "
                  "100.115.92.129 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j apply_vpn_mark -w");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = kTestPID;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::kUser;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_ipv4_subnet = std::make_unique<Subnet>(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 128}, 30),
      base::DoNothing());
  nsinfo.host_ipv4_cidr = *nsinfo.peer_ipv4_subnet->CIDRAtOffset(1);
  nsinfo.peer_ipv4_cidr = *nsinfo.peer_ipv4_subnet->CIDRAtOffset(2);
  nsinfo.static_ipv6_config = std::nullopt;
  nsinfo.peer_mac_addr = peer_mac;
  nsinfo.host_mac_addr = host_mac;

  datapath_.StartRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StopRoutingNamespace) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -o arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -i arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X PREROUTING_arc_ns0 -w");
  Verify_ip_netns_delete(*runner_, "netns_foo");
  Verify_ip(*runner_, "link delete arc_ns0");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = kTestPID;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::kUser;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_ipv4_subnet = std::make_unique<Subnet>(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 128}, 30),
      base::DoNothing());

  datapath_.StopRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StartRoutingNamespace_StaticIPv6) {
  MacAddress peer_mac = {1, 2, 3, 4, 5, 6};
  MacAddress host_mac = {6, 5, 4, 3, 2, 1};

  Verify_ip_netns_delete(*runner_, "netns_foo");
  Verify_ip_netns_attach(*runner_, "netns_foo", kTestPID);
  Verify_ip(*runner_,
            "link add arc_ns0 type veth peer name veth0 netns netns_foo");
  Verify_ip(*runner_,
            "addr add 100.115.92.130/30 brd 100.115.92.131 dev veth0");
  Verify_ip(*runner_, "addr add fd11::2/64 dev veth0");
  Verify_ip(*runner_,
            "link set dev veth0 up addr 01:02:03:04:05:06 multicast off");
  Verify_ip(*runner_, "link set arc_ns0 up");
  Verify_ip(*runner_,
            "addr add 100.115.92.129/30 brd 100.115.92.131 dev arc_ns0");
  Verify_ip(*runner_, "addr add fd11::1/64 dev arc_ns0");
  Verify_ip(*runner_,
            "link set dev arc_ns0 up addr 06:05:04:03:02:01 multicast off");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -o arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -i arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                  "0x00000001/0x00000001 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                  "0x00000200/0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j CONNMARK "
                  "--restore-mark --mask 0xffff0000 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_arc_ns0 -s 100.115.92.130 -d "
                  "100.115.92.129 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "mangle -A PREROUTING_arc_ns0 -s fd11::2 -d "
                  "fd11::1 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_ns0 -j apply_vpn_mark -w");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = kTestPID;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::kUser;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_ipv4_subnet = std::make_unique<Subnet>(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 128}, 30),
      base::DoNothing());
  nsinfo.host_ipv4_cidr = *nsinfo.peer_ipv4_subnet->CIDRAtOffset(1);
  nsinfo.peer_ipv4_cidr = *nsinfo.peer_ipv4_subnet->CIDRAtOffset(2);
  nsinfo.static_ipv6_config = {
      .host_cidr = *IPv6CIDR::CreateFromCIDRString("fd11::1/64"),
      .peer_cidr = *IPv6CIDR::CreateFromCIDRString("fd11::2/64")};
  nsinfo.peer_mac_addr = peer_mac;
  nsinfo.host_mac_addr = host_mac;

  datapath_.StartRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StopRoutingNamespace_StaticIPv6) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -o arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -i arc_ns0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_arc_ns0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X PREROUTING_arc_ns0 -w");
  Verify_ip_netns_delete(*runner_, "netns_foo");
  Verify_ip(*runner_, "link delete arc_ns0");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = kTestPID;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::kUser;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_ipv4_subnet = std::make_unique<Subnet>(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 128}, 30),
      base::DoNothing());
  nsinfo.static_ipv6_config = {
      .host_cidr = *IPv6CIDR::CreateFromCIDRString("fd11::1/64"),
      .peer_cidr = *IPv6CIDR::CreateFromCIDRString("fd11::2/64")};

  datapath_.StopRoutingNamespace(nsinfo);
}
TEST_F(DatapathTest, StartDownstreamTetheredNetwork) {
  EXPECT_CALL(system_, IfNametoindex("wwan0")).WillRepeatedly(Return(4));
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -F accept_downstream_network -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -I INPUT -i ap0 -j accept_downstream_network -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "filter -I accept_downstream_network -p udp --dport 67 "
                  "--sport 68 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -o ap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -i ap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N PREROUTING_ap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_ap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i ap0 -j PREROUTING_ap0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                  "0x00000001/0x00000001 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                  "0x00002300/0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                  "0x03ec0000/0xffff0000 -w");
  Verify_ip(*runner_, "addr add 172.17.49.1/24 brd 172.17.49.255 dev ap0");
  Verify_ip(*runner_, "link set dev ap0 up multicast on");

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kTethering;
  info.upstream_device = ShillClient::Device();
  info.upstream_device->ifname = "wwan0";
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_.StartDownstreamNetwork(info);
}

TEST_F(DatapathTest, StartDownstreamLocalOnlyNetwork) {
  EXPECT_CALL(*runner_, iptables).Times(0);
  EXPECT_CALL(*runner_, ip6tables).Times(0);
  EXPECT_CALL(*runner_, ip).Times(0);

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kLocalOnly;
  info.upstream_device = ShillClient::Device();
  info.upstream_device->ifname = "wwan0";
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_.StartDownstreamNetwork(info);
}

TEST_F(DatapathTest, StopDownstreamTetheredNetwork) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -F accept_downstream_network -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D INPUT -i ap0 -j accept_downstream_network -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -o ap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -i ap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i ap0 -j PREROUTING_ap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_ap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X PREROUTING_ap0 -w");
  EXPECT_CALL(*runner_, ip(_, _, _, _, _)).Times(0);

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kTethering;
  info.upstream_device = ShillClient::Device();
  info.upstream_device->ifname = "wwan0";
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_.StopDownstreamNetwork(info);
}

TEST_F(DatapathTest, StopDownstreamLocalOnlyNetwork) {
  EXPECT_CALL(*runner_, iptables).Times(0);
  EXPECT_CALL(*runner_, ip6tables).Times(0);
  EXPECT_CALL(*runner_, ip).Times(0);

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kLocalOnly;
  info.upstream_device = ShillClient::Device();
  info.upstream_device->ifname = "wwan0";
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_.StopDownstreamNetwork(info);
}

TEST_F(DatapathTest, StartRoutingNewNamespace) {
  MacAddress mac = {1, 2, 3, 4, 5, 6};

  // The running may fail at checking ScopedNS.IsValid() in
  // Datapath::ConnectVethPair(), so we only check if `ip netns add` is invoked
  // correctly here.
  Verify_ip_netns_add(*runner_, "netns_foo");

  ConnectedNamespace nsinfo = {};
  nsinfo.pid = ConnectedNamespace::kNewNetnsPid;
  nsinfo.netns_name = "netns_foo";
  nsinfo.source = TrafficSource::kUser;
  nsinfo.outbound_ifname = "";
  nsinfo.route_on_vpn = true;
  nsinfo.host_ifname = "arc_ns0";
  nsinfo.peer_ifname = "veth0";
  nsinfo.peer_ipv4_subnet = std::make_unique<Subnet>(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 128}, 30),
      base::DoNothing());
  nsinfo.peer_mac_addr = mac;

  datapath_.StartRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StartRoutingDevice) {
  EXPECT_CALL(system_, IfNametoindex("eth0")).WillRepeatedly(Return(2));
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -o arc_eth0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -i arc_eth0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -N PREROUTING_arc_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -F PREROUTING_arc_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i arc_eth0 -j PREROUTING_arc_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_arc_eth0 -j MARK --set-mark "
                  "0x00000001/0x00000001 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_eth0 -j MARK --set-mark "
                  "0x00002000/0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arc_eth0 -j MARK --set-mark "
                  "0x03ea0000/0xffff0000 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.StartRoutingDevice(eth_device, "arc_eth0", TrafficSource::kArc);
}

TEST_F(DatapathTest, StartRoutingDeviceAsUser) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -o vmtap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -i vmtap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N PREROUTING_vmtap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_vmtap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i vmtap0 -j PREROUTING_vmtap0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_vmtap0 -j MARK --set-mark "
                  "0x00000001/0x00000001 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_vmtap0 -j MARK --set-mark "
                  "0x00002100/0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_vmtap0 -j CONNMARK --restore-mark "
                  "--mask 0xffff0000 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_vmtap0 -j skip_apply_vpn_mark -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_vmtap0 -j apply_vpn_mark -w");

  datapath_.StartRoutingDeviceAsUser("vmtap0", TrafficSource::kCrostiniVM,
                                     IPv4Address(1, 2, 3, 4),
                                     /*int_ipv4_addr=*/std::nullopt);
}

TEST_F(DatapathTest, StopRoutingDevice) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -o arc_eth0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -i arc_eth0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i arc_eth0 -j PREROUTING_arc_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -F PREROUTING_arc_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -X PREROUTING_arc_eth0 -w");

  datapath_.StopRoutingDevice("arc_eth0", TrafficSource::kArc);
}

TEST_F(DatapathTest, StopRoutingDeviceAsUser) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -o vmtap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -i vmtap0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i vmtap0 -j PREROUTING_vmtap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_vmtap0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X PREROUTING_vmtap0 -w");

  datapath_.StopRoutingDevice("vmtap0", TrafficSource::kCrostiniVM);
}

TEST_F(DatapathTest, StartStopConnectionPinning) {
  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  // Setup
  EXPECT_CALL(system_, IfNametoindex("eth0")).WillRepeatedly(Return(3));
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N POSTROUTING_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F POSTROUTING_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING -o eth0 -j POSTROUTING_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING_eth0 -j CONNMARK --set-mark "
                  "0x03eb0000/0xffff0000 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING_eth0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i eth0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  datapath_.StartConnectionPinning(eth_device);
  Mock::VerifyAndClearExpectations(runner_);

  // Teardown
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F POSTROUTING_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D POSTROUTING -o eth0 -j POSTROUTING_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X POSTROUTING_eth0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i eth0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  datapath_.StopConnectionPinning(eth_device);
}

TEST_F(DatapathTest, StartStopVpnRouting_ArcVpn) {
  ShillClient::Device vpn_device;
  vpn_device.ifname = "arcbr0";

  // Setup
  EXPECT_CALL(system_, IfNametoindex("arcbr0")).WillRepeatedly(Return(5));
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N POSTROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F POSTROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING -o arcbr0 -j POSTROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING_arcbr0 -j CONNMARK "
                  "--set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -A apply_vpn_mark -m mark ! --mark 0x0/0xffff0000 -j ACCEPT -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -A apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING_arcbr0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i arcbr0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A POSTROUTING -o arcbr0 -j MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
                  "redirect_dns -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A vpn_accept -m mark "
                  "--mark 0x03ed0000/0xffff0000 -j ACCEPT -w");
  datapath_.StartVpnRouting(vpn_device);
  Mock::VerifyAndClearExpectations(runner_);

  // Teardown
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F POSTROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D POSTROUTING -o arcbr0 -j POSTROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X POSTROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F apply_vpn_mark -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i arcbr0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D POSTROUTING -o arcbr0 -j MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
                  "redirect_dns -w");
  Verify_iptables(*runner_, IpFamily::kDual, "filter -F vpn_accept -w");
  datapath_.StopVpnRouting(vpn_device);
}

TEST_F(DatapathTest, StartStopVpnRouting_HostVpn) {
  ShillClient::Device vpn_device;
  vpn_device.ifname = "tun0";

  // Setup
  EXPECT_CALL(system_, IfNametoindex("tun0")).WillRepeatedly(Return(5));
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N POSTROUTING_tun0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F POSTROUTING_tun0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING -o tun0 -j POSTROUTING_tun0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING_tun0 -j CONNMARK --set-mark "
                  "0x03ed0000/0xffff0000 -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -A apply_vpn_mark -m mark ! --mark 0x0/0xffff0000 -j ACCEPT -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -A apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING_tun0 -j CONNMARK "
                  "--save-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i tun0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A POSTROUTING -o tun0 -j MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
                  "redirect_dns -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A vpn_accept -m mark "
                  "--mark 0x03ed0000/0xffff0000 -j ACCEPT -w");
  // Start arcbr0 routing
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -o arcbr0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A FORWARD -i arcbr0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -N PREROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING -i arcbr0 -j PREROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "mangle -A PREROUTING_arcbr0 -j MARK --set-mark "
                  "0x00000001/0x00000001 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arcbr0 -j MARK --set-mark "
                  "0x00002000/0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A PREROUTING_arcbr0 -j MARK --set-mark "
                  "0x03ed0000/0xffff0000 -w");
  datapath_.StartVpnRouting(vpn_device);
  Mock::VerifyAndClearExpectations(runner_);

  // Teardown
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F POSTROUTING_tun0 -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D POSTROUTING -o tun0 -j POSTROUTING_tun0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X POSTROUTING_tun0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F apply_vpn_mark -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i tun0 -j CONNMARK "
                  "--restore-mark --mask 0x00003f00 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D POSTROUTING -o tun0 -j MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
                  "redirect_dns -w");
  Verify_iptables(*runner_, IpFamily::kDual, "filter -F vpn_accept -w");
  // Stop arcbr0 routing
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -o arcbr0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -D FORWARD -i arcbr0 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D PREROUTING -i arcbr0 -j PREROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -F PREROUTING_arcbr0 -w");
  Verify_iptables(*runner_, IpFamily::kDual, "mangle -X PREROUTING_arcbr0 -w");
  datapath_.StopVpnRouting(vpn_device);
}

TEST_F(DatapathTest, AddInboundIPv4DNATArc) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_arc -i eth0 -m socket "
                  "--nowildcard -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_arc -i eth0 -p tcp -j DNAT "
                  "--to-destination 1.2.3.4 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_arc -i eth0 -p udp -j DNAT "
                  "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.AddInboundIPv4DNAT(AutoDNATTarget::kArc, eth_device,
                               IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, RemoveInboundIPv4DNATArc) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_arc -i eth0 -m socket "
                  "--nowildcard -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_arc -i eth0 -p tcp -j DNAT "
                  "--to-destination 1.2.3.4 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_arc -i eth0 -p udp -j DNAT "
                  "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.RemoveInboundIPv4DNAT(AutoDNATTarget::kArc, eth_device,
                                  IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, AddInboundIPv4DNATCrostini) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_crostini -i eth0 -m socket "
                  "--nowildcard -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_crostini -i eth0 -p tcp -j DNAT "
                  "--to-destination 1.2.3.4 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_crostini -i eth0 -p udp -j DNAT "
                  "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.AddInboundIPv4DNAT(AutoDNATTarget::kCrostini, eth_device,
                               IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, RemoveInboundIPv4DNATCrostini) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_crostini -i eth0 -m socket "
                  "--nowildcard -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_crostini -i eth0 -p tcp -j DNAT "
                  "--to-destination 1.2.3.4 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_crostini -i eth0 -p udp -j DNAT "
                  "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.RemoveInboundIPv4DNAT(AutoDNATTarget::kCrostini, eth_device,
                                  IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, AddInboundIPv4DNATParallelsVm) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_parallels -i eth0 -m socket "
                  "--nowildcard -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_parallels -i eth0 -p tcp -j DNAT "
                  "--to-destination 1.2.3.4 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A apply_auto_dnat_to_parallels -i eth0 -p udp -j DNAT "
                  "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.AddInboundIPv4DNAT(AutoDNATTarget::kParallels, eth_device,
                               IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, RemoveInboundIPv4DNATParallelsVm) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_parallels -i eth0 -m socket "
                  "--nowildcard -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_parallels -i eth0 -p tcp -j DNAT "
                  "--to-destination 1.2.3.4 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D apply_auto_dnat_to_parallels -i eth0 -p udp -j DNAT "
                  "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_.RemoveInboundIPv4DNAT(AutoDNATTarget::kParallels, eth_device,
                                  IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, MaskInterfaceFlags) {
  bool result = datapath_.MaskInterfaceFlags("foo0", IFF_DEBUG);

  EXPECT_TRUE(result);
  std::vector<ioctl_req_t> expected = {SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddIPv6HostRoute) {
  Verify_ip6(*runner_, "route replace 2001:da8:e00::1234/128 dev eth0");

  datapath_.AddIPv6HostRoute("eth0", *net_base::IPv6CIDR::CreateFromCIDRString(
                                         "2001:da8:e00::1234/128"));
}

TEST_F(DatapathTest, AddIPv4RouteToTable) {
  Verify_ip(*runner_, "route add 192.0.2.2/24 dev eth0 table 123");

  datapath_.AddIPv4RouteToTable(
      "eth0", *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.2/24"), 123);
}

TEST_F(DatapathTest, DeleteIPv4RouteFromTable) {
  Verify_ip(*runner_, "route del 192.0.2.2/24 dev eth0 table 123");

  datapath_.DeleteIPv4RouteFromTable(
      "eth0", *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.2/24"), 123);
}

TEST_F(DatapathTest, AddIPv4Route) {
  datapath_.AddIPv4Route(IPv4Address(192, 168, 1, 1),
                         *IPv4CIDR::CreateFromCIDRString("100.115.93.0/24"));
  datapath_.DeleteIPv4Route(IPv4Address(192, 168, 1, 1),
                            *IPv4CIDR::CreateFromCIDRString("100.115.93.0/24"));

  std::vector<ioctl_req_t> expected_reqs = {SIOCADDRT, SIOCDELRT};
  EXPECT_EQ(expected_reqs, system_.ioctl_reqs);

  std::string route1 =
      "{rt_dst: {family: AF_INET, port: 0, addr: 100.115.93.0}, rt_genmask: "
      "{family: AF_INET, port: 0, addr: 255.255.255.0}, rt_gateway: {family: "
      "AF_INET, port: 0, addr: 192.168.1.1}, rt_dev: null, rt_flags: RTF_UP | "
      "RTF_GATEWAY}";
  std::vector<std::string> captured_routes;
  for (const auto& route : system_.ioctl_rtentry_args) {
    std::ostringstream stream;
    stream << route.second;
    captured_routes.emplace_back(stream.str());
  }
  EXPECT_EQ(route1, captured_routes[0]);
  EXPECT_EQ(route1, captured_routes[1]);
}

TEST_F(DatapathTest, RedirectDnsRules) {
  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";
  ShillClient::Device wlan_device;
  wlan_device.ifname = "wlan0";

  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_dns -p tcp --dport 53 -o eth0 -j DNAT "
                  "--to-destination 192.168.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_dns -p udp --dport 53 -o eth0 -j DNAT "
                  "--to-destination 192.168.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 1.1.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 1.1.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 1.1.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 1.1.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 8.8.8.8 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 8.8.8.8 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_dns -p tcp --dport 53 -o eth0 -j DNAT "
                  "--to-destination 192.168.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_dns -p udp --dport 53 -o eth0 -j DNAT "
                  "--to-destination 192.168.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 8.8.8.8 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
                  "--to-destination 8.8.8.8 -w");

  datapath_.RemoveRedirectDnsRule(wlan_device);
  datapath_.RemoveRedirectDnsRule(ShillClient::Device());
  datapath_.AddRedirectDnsRule(eth_device, "192.168.1.1");
  datapath_.AddRedirectDnsRule(wlan_device, "1.1.1.1");
  datapath_.AddRedirectDnsRule(wlan_device, "8.8.8.8");
  datapath_.RemoveRedirectDnsRule(eth_device);
  datapath_.RemoveRedirectDnsRule(wlan_device);
}

TEST_F(DatapathTest, DumpIptables) {
  EXPECT_CALL(*runner_,
              iptables(Iptables::Table::kMangle, Iptables::Command::kL,
                       StrEq(""), ElementsAre("-x", "-v", "-n", "-w"), _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>("<iptables output>"), Return(0)));
  EXPECT_CALL(*runner_, ip6tables(Iptables::Table::kMangle,
                                  Iptables::Command::kL, StrEq(""),
                                  ElementsAre("-x", "-v", "-n", "-w"), _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>("<ip6tables output>"), Return(0)));

  EXPECT_EQ("<iptables output>",
            datapath_.DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle));
  EXPECT_EQ("<ip6tables output>",
            datapath_.DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle));
  EXPECT_EQ("",
            datapath_.DumpIptables(IpFamily::kDual, Iptables::Table::kMangle));
}

TEST_F(DatapathTest, SetVpnLockdown) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "filter -A vpn_lockdown -m mark --mark 0x00008000/0x0000c000 "
                  "-j REJECT -w");
  Verify_iptables(*runner_, IpFamily::kDual, "filter -F vpn_lockdown -w");

  datapath_.SetVpnLockdown(true);
  datapath_.SetVpnLockdown(false);
}

TEST_F(DatapathTest, SetConntrackHelpers) {
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kConntrackHelper, "1", ""));
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kConntrackHelper, "0", ""));

  datapath_.SetConntrackHelpers(true);
  datapath_.SetConntrackHelpers(false);
}

TEST_F(DatapathTest, StartDnsRedirection_Default) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
                  "DNAT --to-destination 100.115.92.130 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A redirect_default_dns -i vmtap0 -p tcp --dport 53 -j "
                  "DNAT --to-destination 100.115.92.130 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -A redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
                  "DNAT --to-destination ::1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -A redirect_default_dns -i vmtap0 -p tcp --dport 53 -j "
                  "DNAT --to-destination ::1 -w");

  DnsRedirectionRule rule4 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
      .input_ifname = "vmtap0",
      .proxy_address = *net_base::IPAddress::CreateFromString("100.115.92.130"),
  };
  DnsRedirectionRule rule6 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
      .input_ifname = "vmtap0",
      .proxy_address = *net_base::IPAddress::CreateFromString("::1"),
  };

  datapath_.StartDnsRedirection(rule4);
  datapath_.StartDnsRedirection(rule6);
}

TEST_F(DatapathTest, StartDnsRedirection_User) {
  Sequence sequence;

  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv4,
      "nat -A redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 3 --packet "
      "0 -j DNAT --to-destination 8.8.8.8 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv4,
      "nat -A redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 8.4.8.4 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv4,
      "nat -A redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 1.1.1.1 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv4,
      "nat -A redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 3 --packet "
      "0 -j DNAT --to-destination 8.8.8.8 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv4,
      "nat -A redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 8.4.8.4 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv4,
      "nat -A redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 1.1.1.1 -w",
      sequence);
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A redirect_user_dns -p udp --dport 53 -j DNAT "
                  "--to-destination 100.115.92.130 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -A redirect_user_dns -p tcp --dport 53 -j DNAT "
                  "--to-destination 100.115.92.130 -w");

  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv6,
      "nat -A redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8888 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv6,
      "nat -A redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8844 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv6,
      "nat -A redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8888 -w",
      sequence);
  Verify_iptables_in_sequence(
      *runner_, IpFamily::kIPv6,
      "nat -A redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8844 -w",
      sequence);
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -A snat_user_dns -p udp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -A snat_user_dns -p tcp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -A redirect_user_dns -p udp --dport 53 -j DNAT "
                  "--to-destination ::1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -A redirect_user_dns -p tcp --dport 53 -j DNAT "
                  "--to-destination ::1 -w");

  Verify_iptables(*runner_, IpFamily::kDual,
                  "nat -A snat_chrome_dns -p udp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "nat -A snat_chrome_dns -p tcp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -A skip_apply_vpn_mark -p udp --dport 53 -j ACCEPT -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -A skip_apply_vpn_mark -p tcp --dport 53 -j ACCEPT -w");

  DnsRedirectionRule rule4 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::USER,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("100.115.92.130"),
      .nameservers = {*net_base::IPAddress::CreateFromString("8.8.8.8"),
                      *net_base::IPAddress::CreateFromString("8.4.8.4"),
                      *net_base::IPAddress::CreateFromString("1.1.1.1")},
  };
  DnsRedirectionRule rule6 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::USER,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("::1"),
      .nameservers =
          {*net_base::IPAddress::CreateFromString("2001:4860:4860::8888"),
           *net_base::IPAddress::CreateFromString("2001:4860:4860::8844")},
  };

  datapath_.StartDnsRedirection(rule4);
  datapath_.StartDnsRedirection(rule6);
}

TEST_F(DatapathTest, StartDnsRedirection_ExcludeDestination) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_chrome_dns -p udp ! -d 100.115.92.130 "
                  "--dport 53 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_chrome_dns -p tcp ! -d 100.115.92.130 "
                  "--dport 53 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_user_dns -p udp ! -d 100.115.92.130 --dport "
                  "53 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -I redirect_user_dns -p tcp ! -d 100.115.92.130 --dport "
                  "53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -I redirect_chrome_dns -p udp ! -d ::1 --dport 53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -I redirect_chrome_dns -p tcp ! -d ::1 --dport 53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -I redirect_user_dns -p udp ! -d ::1 --dport 53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -I redirect_user_dns -p tcp ! -d ::1 --dport 53 -j RETURN -w");

  DnsRedirectionRule rule4 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("100.115.92.130"),
  };
  DnsRedirectionRule rule6 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("::1"),
  };

  datapath_.StartDnsRedirection(rule4);
  datapath_.StartDnsRedirection(rule6);
}

TEST_F(DatapathTest, StopDnsRedirection_Default) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
                  "DNAT --to-destination 100.115.92.130 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_default_dns -i vmtap0 -p tcp --dport 53 -j "
                  "DNAT --to-destination 100.115.92.130 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -D redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
                  "DNAT --to-destination ::1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -D redirect_default_dns -i vmtap0 -p tcp --dport 53 -j "
                  "DNAT --to-destination ::1 -w");

  DnsRedirectionRule rule4 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
      .input_ifname = "vmtap0",
      .proxy_address = *net_base::IPAddress::CreateFromString("100.115.92.130"),
  };
  DnsRedirectionRule rule6 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
      .input_ifname = "vmtap0",
      .proxy_address = *net_base::IPAddress::CreateFromString("::1"),
  };

  datapath_.StopDnsRedirection(rule4);
  datapath_.StopDnsRedirection(rule6);
}

TEST_F(DatapathTest, StopDnsRedirection_User) {
  Verify_iptables(
      *runner_, IpFamily::kIPv4,
      "nat -D redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 3 --packet "
      "0 -j DNAT --to-destination 8.8.8.8 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv4,
      "nat -D redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 8.4.8.4 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv4,
      "nat -D redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 1.1.1.1 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv4,
      "nat -D redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 3 --packet "
      "0 -j DNAT --to-destination 8.8.8.8 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv4,
      "nat -D redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 8.4.8.4 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv4,
      "nat -D redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 1.1.1.1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_user_dns -p udp --dport 53 -j DNAT "
                  "--to-destination 100.115.92.130 -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_user_dns -p tcp --dport 53 -j DNAT "
                  "--to-destination 100.115.92.130 -w");

  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8888 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_chrome_dns -p udp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8844 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 2 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8888 -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_chrome_dns -p tcp --dport 53 -m owner "
      "--uid-owner chronos -m statistic --mode nth --every 1 --packet "
      "0 -j DNAT --to-destination 2001:4860:4860::8844 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -D snat_user_dns -p udp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -D snat_user_dns -p tcp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -D redirect_user_dns -p udp --dport 53 -j DNAT "
                  "--to-destination ::1 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "nat -D redirect_user_dns -p tcp --dport 53 -j DNAT "
                  "--to-destination ::1 -w");

  Verify_iptables(*runner_, IpFamily::kDual,
                  "nat -D snat_chrome_dns -p udp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(*runner_, IpFamily::kDual,
                  "nat -D snat_chrome_dns -p tcp --dport 53 -j "
                  "MASQUERADE -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -D skip_apply_vpn_mark -p udp --dport 53 -j ACCEPT -w");
  Verify_iptables(
      *runner_, IpFamily::kDual,
      "mangle -D skip_apply_vpn_mark -p tcp --dport 53 -j ACCEPT -w");

  DnsRedirectionRule rule4 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::USER,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("100.115.92.130"),
      .nameservers = {*net_base::IPAddress::CreateFromString("8.8.8.8"),
                      *net_base::IPAddress::CreateFromString("8.4.8.4"),
                      *net_base::IPAddress::CreateFromString("1.1.1.1")},
  };
  DnsRedirectionRule rule6 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::USER,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("::1"),
      .nameservers =
          {*net_base::IPAddress::CreateFromString("2001:4860:4860::8888"),
           *net_base::IPAddress::CreateFromString("2001:4860:4860::8844")},
  };

  datapath_.StopDnsRedirection(rule4);
  datapath_.StopDnsRedirection(rule6);
}

TEST_F(DatapathTest, StopDnsRedirection_ExcludeDestination) {
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_chrome_dns -p udp ! -d 100.115.92.130 "
                  "--dport 53 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_chrome_dns -p tcp ! -d 100.115.92.130 "
                  "--dport 53 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_user_dns -p udp ! -d 100.115.92.130 --dport "
                  "53 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv4,
                  "nat -D redirect_user_dns -p tcp ! -d 100.115.92.130 --dport "
                  "53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_chrome_dns -p udp ! -d ::1 --dport 53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_chrome_dns -p tcp ! -d ::1 --dport 53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_user_dns -p udp ! -d ::1 --dport 53 -j RETURN -w");
  Verify_iptables(
      *runner_, IpFamily::kIPv6,
      "nat -D redirect_user_dns -p tcp ! -d ::1 --dport 53 -j RETURN -w");

  DnsRedirectionRule rule4 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("100.115.92.130"),
  };
  DnsRedirectionRule rule6 = {
      .type = patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION,
      .input_ifname = "",
      .proxy_address = *net_base::IPAddress::CreateFromString("::1"),
  };

  datapath_.StopDnsRedirection(rule4);
  datapath_.StopDnsRedirection(rule6);
}

TEST_F(DatapathTest, PrefixEnforcement) {
  ShillClient::Device cell_device;
  cell_device.ifname = "wwan0";

  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -N egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -I OUTPUT -o wwan0 -j egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_.StartSourceIPv6PrefixEnforcement(cell_device);

  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A egress_wwan0 -s 2001:db8:1:1::/64 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_.UpdateSourceEnforcementIPv6Prefix(
      cell_device,
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1:1::/64"));

  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_.UpdateSourceEnforcementIPv6Prefix(cell_device, std::nullopt);

  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A egress_wwan0 -s 2001:db8:1:2::/64 -j RETURN -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_.UpdateSourceEnforcementIPv6Prefix(
      cell_device,
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1:2::/64"));

  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -D OUTPUT -o wwan0 -j egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  Verify_iptables(*runner_, IpFamily::kIPv6, "filter -X egress_wwan0 -w");
  datapath_.StopSourceIPv6PrefixEnforcement(cell_device);
}

TEST_F(DatapathTest, SetRouteLocalnet) {
  EXPECT_CALL(system_,
              SysNetSet(System::SysNet::kIPv4RouteLocalnet, "1", "eth0"));
  EXPECT_CALL(system_,
              SysNetSet(System::SysNet::kIPv4RouteLocalnet, "0", "wlan0"));

  datapath_.SetRouteLocalnet("eth0", true);
  datapath_.SetRouteLocalnet("wlan0", false);
}

TEST_F(DatapathTest, ModprobeAll) {
  EXPECT_CALL(*runner_, modprobe_all(ElementsAre("ip6table_filter", "ah6",
                                                 "esp6", "nf_nat_ftp"),
                                     _));

  datapath_.ModprobeAll({"ip6table_filter", "ah6", "esp6", "nf_nat_ftp"});
}

TEST_F(DatapathTest, ModifyPortRule) {
  patchpanel::ModifyPortRuleRequest request;
  request.set_input_ifname("eth0");
  request.set_input_dst_ip("192.168.1.1");
  request.set_input_dst_port(80);
  request.set_dst_ip("100.115.92.14");
  request.set_dst_port(8080);

  // Invalid request #1
  request.set_op(patchpanel::ModifyPortRuleRequest::INVALID_OPERATION);
  request.set_proto(patchpanel::ModifyPortRuleRequest::TCP);
  request.set_type(patchpanel::ModifyPortRuleRequest::ACCESS);
  EXPECT_CALL(*firewall_, AddAcceptRules(_, _, _)).Times(0);
  EXPECT_FALSE(datapath_.ModifyPortRule(request));
  Mock::VerifyAndClearExpectations(firewall_);

  // Invalid request #2
  request.set_op(patchpanel::ModifyPortRuleRequest::CREATE);
  request.set_proto(patchpanel::ModifyPortRuleRequest::INVALID_PROTOCOL);
  request.set_type(patchpanel::ModifyPortRuleRequest::ACCESS);
  EXPECT_CALL(*firewall_, AddAcceptRules(_, _, _)).Times(0);
  EXPECT_FALSE(datapath_.ModifyPortRule(request));
  Mock::VerifyAndClearExpectations(firewall_);

  // Invalid request #3
  request.set_op(patchpanel::ModifyPortRuleRequest::CREATE);
  request.set_proto(patchpanel::ModifyPortRuleRequest::TCP);
  request.set_type(patchpanel::ModifyPortRuleRequest::INVALID_RULE_TYPE);
  EXPECT_CALL(*firewall_, AddAcceptRules(_, _, _)).Times(0);
  EXPECT_FALSE(datapath_.ModifyPortRule(request));
  Mock::VerifyAndClearExpectations(firewall_);
}

TEST_F(DatapathTest, EnableQoSDetection) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A qos_detect_static -j qos_detect -w");

  datapath_.EnableQoSDetection();
}

TEST_F(DatapathTest, DisableQoSDetection) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D qos_detect_static -j qos_detect -w");

  datapath_.DisableQoSDetection();
}

TEST_F(DatapathTest, EnableQoSApplyingDSCP) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A POSTROUTING -o wlan0 -j qos_apply_dscp -w");

  datapath_.EnableQoSApplyingDSCP("wlan0");
}

TEST_F(DatapathTest, DisableQoSApplyingDSCP) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D POSTROUTING -o wlan0 -j qos_apply_dscp -w");

  datapath_.DisableQoSApplyingDSCP("wlan0");
}

TEST_F(DatapathTest, UpdateDoHProvidersForQoSIPv4) {
  const std::vector<net_base::IPAddress> ipv4_input = {
      net_base::IPAddress::CreateFromString("1.2.3.4").value(),
      net_base::IPAddress::CreateFromString("5.6.7.8").value(),
  };

  Verify_iptables(*runner_, IpFamily::kIPv4, "mangle -F qos_detect_doh -w");
  for (const auto& proto : {"tcp", "udp"}) {
    const std::string expected_rule =
        base::StrCat({"mangle -A qos_detect_doh -p ", proto,
                      " --dport 443 -d 1.2.3.4,5.6.7.8 -j MARK --set-xmark "
                      "0x00000060/0x000000e0 -w"});
    Verify_iptables(*runner_, IpFamily::kIPv4, expected_rule);
  }

  datapath_.UpdateDoHProvidersForQoS(IpFamily::kIPv4, ipv4_input);
}

TEST_F(DatapathTest, UpdateDoHProvidersForQoSIPv6) {
  // Verify IPv6 input.
  const std::vector<net_base::IPAddress> ipv6_input = {
      net_base::IPAddress::CreateFromString("fd00::1").value(),
      net_base::IPAddress::CreateFromString("fd00::2").value(),
  };

  Verify_iptables(*runner_, IpFamily::kIPv6, "mangle -F qos_detect_doh -w");
  for (const auto& proto : {"tcp", "udp"}) {
    const std::string expected_rule =
        base::StrCat({"mangle -A qos_detect_doh -p ", proto,
                      " --dport 443 -d fd00::1,fd00::2 -j MARK --set-xmark "
                      "0x00000060/0x000000e0 -w"});
    Verify_iptables(*runner_, IpFamily::kIPv6, expected_rule);
  }

  datapath_.UpdateDoHProvidersForQoS(IpFamily::kIPv6, ipv6_input);
}

TEST_F(DatapathTest, UpdateDoHProvidersForQoSEmpty) {
  // Empty list should still trigger the flush, but no other rules.
  Verify_iptables(*runner_, IpFamily::kIPv4, "mangle -F qos_detect_doh -w");
  datapath_.UpdateDoHProvidersForQoS(IpFamily::kIPv4, {});
}

TEST_F(DatapathTest, ModifyClatAcceptRules) {
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A FORWARD -i tun_nat64 -j ACCEPT -w");
  Verify_iptables(*runner_, IpFamily::kIPv6,
                  "filter -A FORWARD -o tun_nat64 -j ACCEPT -w");
  datapath_.ModifyClatAcceptRules(Iptables::Command::kA, "tun_nat64");
}

TEST_F(DatapathTest, AddBorealisQosRule) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -A qos_detect_borealis "
                  "-i vmtap6 -j MARK --set-xmark 0x00000020/0x000000e0 -w");
  datapath_.AddBorealisQoSRule("vmtap6");
}

TEST_F(DatapathTest, RemoveBorealisQosRule) {
  Verify_iptables(*runner_, IpFamily::kDual,
                  "mangle -D qos_detect_borealis "
                  "-i vmtap6 -j MARK --set-xmark 0x00000020/0x000000e0 -w");
  datapath_.RemoveBorealisQoSRule("vmtap6");
}

}  // namespace patchpanel

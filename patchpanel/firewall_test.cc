// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/firewall.h"

#include <string>
#include <string_view>
#include <vector>

#include <base/strings/string_split.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"
#include "patchpanel/mock_process_runner.h"

using testing::_;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::Mock;
using testing::Return;
using testing::SaveArg;
using testing::StrEq;

namespace patchpanel {
TEST(FirewallTest, AddAcceptRules_InvalidPorts) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectNoCallIptables();

  EXPECT_FALSE(firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 0, "iface"));
  EXPECT_FALSE(firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 0, "iface"));
  EXPECT_FALSE(
      firewall.DeleteAcceptRules(ModifyPortRuleRequest::TCP, 0, "iface"));
  EXPECT_FALSE(
      firewall.DeleteAcceptRules(ModifyPortRuleRequest::UDP, 0, "iface"));
}

TEST(FirewallTest, AddAcceptRules_ValidInterfaceName) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_port_firewall -p tcp --dport 80 -i "
      "shortname -j ACCEPT -w");
  EXPECT_TRUE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "shortname"));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_port_firewall -p udp --dport 53 -i "
      "shortname -j ACCEPT -w");
  EXPECT_TRUE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "shortname"));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_port_firewall -p tcp --dport 80 -i "
      "middle-dash -j ACCEPT -w");
  EXPECT_TRUE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "middle-dash"));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_port_firewall -p udp --dport 53 -i "
      "middle-dash -j ACCEPT -w");
  EXPECT_TRUE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "middle-dash"));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_port_firewall -p tcp --dport 80 -i "
      "middle.dot -j ACCEPT -w");
  EXPECT_TRUE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "middle.dot"));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_port_firewall -p udp --dport 53 -i "
      "middle.dot -j ACCEPT -w");
  EXPECT_TRUE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "middle.dot"));
  Mock::VerifyAndClearExpectations(&runner);
}

TEST(FirewallTest, AddAcceptRules_IptablesFails) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -I ingress_port_firewall -p tcp --dport 80 -i iface -j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -I ingress_port_firewall -p udp --dport 53 -i iface -j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectNoCallIptables(IpFamily::kIPv6);
  // Punch hole for TCP port 80, should fail.
  ASSERT_FALSE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "iface"));
  // Punch hole for UDP port 53, should fail.
  ASSERT_FALSE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "iface"));
}

TEST(FirewallTest, AddAcceptRules_Ip6tablesFails) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -I ingress_port_firewall -p tcp --dport 80 -i iface "
      "-j ACCEPT -w");
  // Mock that failed to accept IPv6 rules for TCP.
  runner.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -I ingress_port_firewall -p tcp --dport 80 -i iface "
      "-j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D ingress_port_firewall -p tcp --dport 80 -i iface "
      "-j ACCEPT -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -I ingress_port_firewall -p udp --dport 53 -i iface "
      "-j ACCEPT -w");
  // Mock that failed to accept IPv6 rules for UDP.
  runner.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -I ingress_port_firewall -p udp --dport 53 -i iface "
      "-j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D ingress_port_firewall -p udp --dport 53 -i iface "
      "-j ACCEPT -w");

  // Punch hole for TCP port 80, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::TCP, 80, "iface"));
  // Punch hole for UDP port 53, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      firewall.AddAcceptRules(ModifyPortRuleRequest::UDP, 53, "iface"));
}

TEST(FirewallTest, AddLoopbackLockdownRules_InvalidPort) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectNoCallIptables();

  // Try to lock down TCP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 0));
  // Try to lock down UDP port 0, port 0 is not a valid port.
  EXPECT_FALSE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 0));
}

TEST(FirewallTest, AddLoopbackLockdownRules_Success) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I egress_port_firewall -p tcp --dport 80 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  ASSERT_TRUE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 80));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I egress_port_firewall -p udp --dport 53 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  ASSERT_TRUE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 53));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I egress_port_firewall -p tcp --dport 1234 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  ASSERT_TRUE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 1234));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I egress_port_firewall -p tcp --dport 8080 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  ASSERT_TRUE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 8080));
  Mock::VerifyAndClearExpectations(&runner);
}

TEST(FirewallTest, AddLoopbackLockdownRules_IptablesFails) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(IpFamily::kIPv4,
                            "filter -I egress_port_firewall -p tcp --dport 80 "
                            "-o lo -m owner ! --uid-owner chronos -j REJECT -w",
                            /*call_times=*/1, /*output=*/"",
                            /*empty_chain=*/false, /*return_value=*/1);
  runner.ExpectCallIptables(IpFamily::kIPv4,
                            "filter -I egress_port_firewall -p udp --dport 53 "
                            "-o lo -m owner ! --uid-owner chronos -j REJECT -w",
                            /*call_times=*/1, /*output=*/"",
                            /*empty_chain=*/false, /*return_value=*/1);
  runner.ExpectNoCallIptables(IpFamily::kIPv6);

  // Lock down TCP port 80, should fail.
  ASSERT_FALSE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 80));
  // Lock down UDP port 53, should fail.
  ASSERT_FALSE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 53));
}

TEST(FirewallTest, AddLoopbackLockdownRules_Ip6tablesFails) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -I egress_port_firewall -p tcp --dport 80 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -I egress_port_firewall -p tcp --dport 80 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D egress_port_firewall -p tcp --dport 80 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -I egress_port_firewall -p udp --dport 53 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -I egress_port_firewall -p udp --dport 53 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D egress_port_firewall -p udp --dport 53 -o lo -m owner ! "
      "--uid-owner chronos -j REJECT -w");

  // Lock down TCP port 80, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::TCP, 80));
  // Lock down UDP port 53, should fail because 'ip6tables' fails.
  ASSERT_FALSE(
      firewall.AddLoopbackLockdownRules(ModifyPortRuleRequest::UDP, 53));
}

TEST(FirewallTest, AddIpv4ForwardRules_InvalidArguments) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  // Empty input interface. No iptables commands are issued.
  runner.ExpectNoCallIptables();
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  // Empty interface. No iptables commands are issued.
  runner.ExpectNoCallIptables();
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  // Trying to delete an IPv4 forward rule with an invalid input port will
  // still trigger an explicit iptables -D command for the associated FORWARD
  // ACCEPT rule. Two such commands are expected.
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i iface -p tcp -d 100.115.92.5 --dport "
      "8080 -j ACCEPT -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i iface -p udp -d 100.115.92.5 --dport "
      "8080 -j ACCEPT -w");
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 0, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 0, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 0, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 0, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  // Invalid output dst port. No iptables commands are issused.
  runner.ExpectNoCallIptables();
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 0));
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 0));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 0));
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 5), 0));
  Mock::VerifyAndClearExpectations(&runner);
}

TEST(FirewallTest, AddIpv4ForwardRules_IptablesFails) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  // AddIpv4ForwardRule: Firewall should return at the first failure and issue
  // only a single command.
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I ingress_port_forwarding -i iface -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectNoCallIptables(IpFamily::kIPv6);
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 6), 8080));
  Mock::VerifyAndClearExpectations(&runner);
}

TEST(FirewallTest, AddIpv4ForwardRules_IptablesPartialFailure) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I ingress_port_forwarding -i iface -p udp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A FORWARD -i iface -p udp -d 100.115.92.6 --dport "
      "8080 -j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i iface -p udp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w");
  runner.ExpectNoCallIptables(IpFamily::kIPv6);
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 6), 8080));
  Mock::VerifyAndClearExpectations(&runner);
}

TEST(FirewallTest, DeleteIpv4ForwardRules_IptablesFails) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  // DeleteIpv4ForwardRule: Firewall should try to delete both the DNAT rule
  // and the FORWARD rule regardless of iptables failures.
  //  Verify_iptables(runner, "nat -D ingress_port_forwarding -i iface -p tcp
  //  --dport 80 -j DNAT --to-destination 100.115.92.6:8080 -w");
  //  Verify_iptables(runner, "filter -D FORWARD -i iface -p tcp -d
  //  100.115.92.6 --dport 8080 -j ACCEPT -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i iface -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w",
      1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i iface -p tcp -d 100.115.92.6 --dport "
      "8080 -j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectNoCallIptables(IpFamily::kIPv6);
  ASSERT_FALSE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 6), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i iface -p udp --dport 80 -j DNAT "
      "--to-destination 100.115.92.6:8080 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i iface -p udp -d 100.115.92.6 --dport "
      "8080 -j ACCEPT -w");
  runner.ExpectNoCallIptables(IpFamily::kIPv6);
  ASSERT_TRUE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 80, "iface",
      net_base::IPv4Address(100, 115, 92, 6), 8080));
  Mock::VerifyAndClearExpectations(&runner);
}

TEST(FirewallTest, AddIpv4ForwardRules_ValidRules) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I ingress_port_forwarding -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A FORWARD -i wlan0 -p tcp -d 100.115.92.2 --dport "
      "8080 -j ACCEPT -w");
  ASSERT_TRUE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "wlan0",
      net_base::IPv4Address(100, 115, 92, 2), 8080));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I ingress_port_forwarding -i vmtap0 -p tcp -d 100.115.92.2 --dport "
      "5555 -j DNAT --to-destination 127.0.0.1:5550 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A FORWARD -i vmtap0 -p tcp -d 127.0.0.1 --dport "
      "5550 -j ACCEPT -w");
  ASSERT_TRUE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, net_base::IPv4Address(100, 115, 92, 2), 5555,
      "vmtap0", net_base::IPv4Address(127, 0, 0, 1), 5550));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I ingress_port_forwarding -i eth0 -p udp --dport 5353 -j DNAT "
      "--to-destination 192.168.1.1:5353 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A FORWARD -i eth0 -p udp -d 192.168.1.1 --dport "
      "5353 -j ACCEPT -w");
  ASSERT_TRUE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 5353, "eth0",
      net_base::IPv4Address(192, 168, 1, 1), 5353));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i mlan0 -p tcp --dport 5000 -j DNAT "
      "--to-destination 10.0.0.24:5001 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i mlan0 -p tcp -d 10.0.0.24 --dport 5001 "
      "-j ACCEPT -w");
  ASSERT_TRUE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 5000, "mlan0",
      net_base::IPv4Address(10, 0, 0, 24), 5001));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i vmtap0 -p tcp -d 100.115.92.2 --dport "
      "5555 -j DNAT --to-destination 127.0.0.1:5550 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i vmtap0 -p tcp -d 127.0.0.1 --dport "
      "5550 -j ACCEPT -w");
  ASSERT_TRUE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, net_base::IPv4Address(100, 115, 92, 2), 5555,
      "vmtap0", net_base::IPv4Address(127, 0, 0, 1), 5550));
  Mock::VerifyAndClearExpectations(&runner);

  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i eth1 -p udp --dport 443 -j DNAT "
      "--to-destination 1.2.3.4:443 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D FORWARD -i eth1 -p udp -d 1.2.3.4 --dport 443 -j ACCEPT -w");
  ASSERT_TRUE(firewall.DeleteIpv4ForwardRule(
      ModifyPortRuleRequest::UDP, std::nullopt, 443, "eth1",
      net_base::IPv4Address(1, 2, 3, 4), 443));
}

TEST(FirewallTest, AddIpv4ForwardRules_PartialFailure) {
  MockProcessRunner runner;
  Firewall firewall(&runner);

  // When the second issued FORWARD command fails, expect a delete command to
  // cleanup the ingress_port_forwarding command that succeeded.
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I ingress_port_forwarding -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w");
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A FORWARD -i wlan0 -p tcp -d 100.115.92.2 --dport "
      "8080 -j ACCEPT -w",
      /*call_times=*/1, /*output=*/"", /*empty_chain=*/false,
      /*return_value=*/1);
  runner.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D ingress_port_forwarding -i wlan0 -p tcp --dport 80 -j DNAT "
      "--to-destination 100.115.92.2:8080 -w");
  ASSERT_FALSE(firewall.AddIpv4ForwardRule(
      ModifyPortRuleRequest::TCP, std::nullopt, 80, "wlan0",
      net_base::IPv4Address(100, 115, 92, 2), 8080));
}
}  // namespace patchpanel

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

#include "patchpanel/mock_firewall.h"
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
  MOCK_METHOD2(modprobe_all,
               int(const std::vector<std::string>& modules, bool log_failures));
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

TEST(DatapathTest, IpFamily) {
  EXPECT_EQ(IpFamily::Dual, IpFamily::IPv4 | IpFamily::IPv6);
  EXPECT_EQ(IpFamily::Dual & IpFamily::IPv4, IpFamily::IPv4);
  EXPECT_EQ(IpFamily::Dual & IpFamily::IPv6, IpFamily::IPv6);
  EXPECT_NE(IpFamily::Dual, IpFamily::IPv4);
  EXPECT_NE(IpFamily::Dual, IpFamily::IPv6);
  EXPECT_NE(IpFamily::IPv4, IpFamily::IPv6);
}

TEST(DatapathTest, Start) {
  MockProcessRunner runner;
  MockFirewall firewall;
  // Asserts for sysctl modifications
  EXPECT_CALL(runner, sysctl_w(StrEq("net.ipv4.ip_forward"), StrEq("1"), true));
  EXPECT_CALL(runner, sysctl_w(StrEq("net.ipv4.ip_local_port_range"),
                               StrEq("32768 47103"), true));
  EXPECT_CALL(runner, sysctl_w(StrEq("net.ipv6.conf.all.forwarding"),
                               StrEq("1"), true));
  // Asserts for AddSNATMarkRules
  EXPECT_CALL(
      runner,
      iptables(StrEq("filter"),
               ElementsAre("-A", "FORWARD", "-m", "mark", "--mark", "1/1", "-m",
                           "state", "--state", "INVALID", "-j", "DROP", "-w"),
               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-A", "FORWARD", "-m", "mark", "--mark",
                                   "1/1", "-j", "ACCEPT", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-A", "POSTROUTING", "-m", "mark", "--mark",
                                   "1/1", "-j", "MASQUERADE", "-w"),
                       true, nullptr));
  // Asserts for AddForwardEstablishedRule
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-A", "FORWARD", "-m", "state", "--state",
                                   "ESTABLISHED,RELATED", "-j", "ACCEPT", "-w"),
                       true, nullptr));
  // Asserts for AddSourceIPv4DropRule() calls.
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "eth+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "wlan+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "mlan+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "usb+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "wwan+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "rmnet+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  // Asserts for AddOutboundIPv4SNATMark("vmtap+")
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "PREROUTING", "-i", "vmtap+", "-j",
                                   "MARK", "--set-mark", "1/1", "-w"),
                       true, nullptr));
  // Asserts for OUTPUT ndp connmark bypass rule
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "OUTPUT", "-p", "icmpv6", "--icmpv6-type",
                            "router-solicitation", "-j", "ACCEPT", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "OUTPUT", "-p", "icmpv6", "--icmpv6-type",
                            "router-advertisement", "-j", "ACCEPT", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "OUTPUT", "-p", "icmpv6", "--icmpv6-type",
                            "neighbour-solicitation", "-j", "ACCEPT", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "OUTPUT", "-p", "icmpv6", "--icmpv6-type",
                            "neighbour-advertisement", "-j", "ACCEPT", "-w"),
                true, nullptr));
  // Asserts for OUTPUT CONNMARK restore rule
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "OUTPUT", "-j", "CONNMARK",
                                           "--restore-mark", "--mask",
                                           "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "OUTPUT", "-j", "CONNMARK",
                                            "--restore-mark", "--mask",
                                            "0xffff0000", "-w"),
                                true, nullptr));
  // Asserts for apply_local_source_mark chain
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-N", "apply_local_source_mark", "-w"), true,
                       nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-F", "apply_local_source_mark", "-w"), true,
                       nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "OUTPUT", "-j",
                                           "apply_local_source_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                           "--uid-owner", "chronos", "-j", "MARK", "--set-mark",
                           "0x00008100/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "apply_local_source_mark",
                                           "-m", "owner", "--uid-owner",
                                           "debugd", "-j", "MARK", "--set-mark",
                                           "0x00008200/0x0000ff00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "apply_local_source_mark", "-m",
                                   "owner", "--uid-owner", "cups", "-j", "MARK",
                                   "--set-mark", "0x00008200/0x0000ff00", "-w"),
                       true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                           "--uid-owner", "kerberosd", "-j", "MARK",
                           "--set-mark", "0x00008400/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                           "--uid-owner", "kerberosd-exec", "-j", "MARK",
                           "--set-mark", "0x00008400/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                           "--uid-owner", "tlsdate", "-j", "MARK", "--set-mark",
                           "0x00008400/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                           "--uid-owner", "pluginvm", "-j", "MARK",
                           "--set-mark", "0x00008200/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                           "--uid-owner", "fuse-smbfs", "-j", "MARK",
                           "--set-mark", "0x00008400/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "cgroup",
                           "--cgroup", "0x00010001", "-j", "MARK", "--set-mark",
                           "0x00000300/0x0000ff00", "-w"),
               true, nullptr));
  EXPECT_CALL(
      runner,
      iptables(StrEq("mangle"),
               ElementsAre("-A", "apply_local_source_mark", "-m", "mark",
                           "--mark", "0x0/0x00003f00", "-j", "MARK",
                           "--set-mark", "0x00000400/0x00003f00", "-w"),
               true, nullptr));
  EXPECT_CALL(runner,
              ip6tables(StrEq("mangle"),
                        ElementsAre("-N", "apply_local_source_mark", "-w"),
                        true, nullptr));
  EXPECT_CALL(runner,
              ip6tables(StrEq("mangle"),
                        ElementsAre("-F", "apply_local_source_mark", "-w"),
                        true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "OUTPUT", "-j",
                                            "apply_local_source_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "debugd", "-j", "MARK", "--set-mark",
                            "0x00008200/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "chronos", "-j", "MARK",
                            "--set-mark", "0x00008100/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "apply_local_source_mark",
                                            "-m", "owner", "--uid-owner",
                                            "cups", "-j", "MARK", "--set-mark",
                                            "0x00008200/0x0000ff00", "-w"),
                                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "kerberosd", "-j", "MARK",
                            "--set-mark", "0x00008400/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "kerberosd-exec", "-j", "MARK",
                            "--set-mark", "0x00008400/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "tlsdate", "-j", "MARK",
                            "--set-mark", "0x00008400/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "pluginvm", "-j", "MARK",
                            "--set-mark", "0x00008200/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "owner",
                            "--uid-owner", "fuse-smbfs", "-j", "MARK",
                            "--set-mark", "0x00008400/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "cgroup",
                            "--cgroup", "0x00010001", "-j", "MARK",
                            "--set-mark", "0x00000300/0x0000ff00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_local_source_mark", "-m", "mark",
                            "--mark", "0x0/0x00003f00", "-j", "MARK",
                            "--set-mark", "0x00000400/0x00003f00", "-w"),
                true, nullptr));
  // Asserts for apply_vpn_mark chain
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-N", "apply_vpn_mark", "-w"), true,
                               nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-F", "apply_vpn_mark", "-w"), true,
                               nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "OUTPUT", "-m", "mark",
                                           "--mark", "0x00008000/0x0000c000",
                                           "-j", "apply_vpn_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "apply_vpn_mark", "-m", "mark",
                                           "!", "--mark", "0x0/0xffff0000",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-N", "apply_vpn_mark", "-w"), true,
                                nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-F", "apply_vpn_mark", "-w"), true,
                                nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "OUTPUT", "-m", "mark",
                                            "--mark", "0x00008000/0x0000c000",
                                            "-j", "apply_vpn_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "apply_vpn_mark", "-m", "mark", "!", "--mark",
                            "0x0/0xffff0000", "-j", "ACCEPT", "-w"),
                true, nullptr));
  // Asserts for check_routing_mark chain
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-N", "check_routing_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-F", "check_routing_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-j",
                                           "CONNMARK", "--restore-mark",
                                           "--mask", "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-m", "mark",
                                           "!", "--mark", "0x0/0xffff0000",
                                           "-j", "check_routing_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-N", "check_routing_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-F", "check_routing_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-j",
                                            "CONNMARK", "--restore-mark",
                                            "--mask", "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-m", "mark",
                                            "!", "--mark", "0x0/0xffff0000",
                                            "-j", "check_routing_mark", "-w"),
                                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.Start();
}

TEST(DatapathTest, Stop) {
  MockProcessRunner runner;
  MockFirewall firewall;
  // Asserts for sysctl modifications
  EXPECT_CALL(runner, sysctl_w(StrEq("net.ipv4.ip_local_port_range"),
                               StrEq("32768 61000"), true));
  EXPECT_CALL(runner, sysctl_w(StrEq("net.ipv6.conf.all.forwarding"),
                               StrEq("0"), true));
  EXPECT_CALL(runner, sysctl_w(StrEq("net.ipv4.ip_forward"), StrEq("0"), true));
  // Asserts for RemoveOutboundIPv4SNATMark("vmtap+")
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "PREROUTING", "-i", "vmtap+", "-j",
                                   "MARK", "--set-mark", "1/1", "-w"),
                       true, nullptr));
  // Asserts for RemoveForwardEstablishedRule
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "FORWARD", "-m", "state", "--state",
                                   "ESTABLISHED,RELATED", "-j", "ACCEPT", "-w"),
                       true, nullptr));
  // Asserts for RemoveSNATMarkRules
  EXPECT_CALL(
      runner,
      iptables(StrEq("filter"),
               ElementsAre("-D", "FORWARD", "-m", "mark", "--mark", "1/1", "-m",
                           "state", "--state", "INVALID", "-j", "DROP", "-w"),
               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "FORWARD", "-m", "mark", "--mark",
                                   "1/1", "-j", "ACCEPT", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-D", "POSTROUTING", "-m", "mark", "--mark",
                                   "1/1", "-j", "MASQUERADE", "-w"),
                       true, nullptr));
  // Asserts for RemoveSourceIPv4DropRule() calls.
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "eth+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "wlan+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "mlan+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "usb+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "wwan+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "rmnet+", "-s",
                                   "100.115.92.0/23", "-j", "DROP", "-w"),
                       true, nullptr));
  // Asserts for apply_local_source_mark chain
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "OUTPUT", "-j",
                                           "apply_local_source_mark", "-w"),
                               true, nullptr));
  // Asserts for OUTPUT CONNMARK restore rule
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "OUTPUT", "-j", "CONNMARK",
                                           "--restore-mark", "--mask",
                                           "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "OUTPUT", "-j", "CONNMARK",
                                            "--restore-mark", "--mask",
                                            "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-F", "apply_local_source_mark", "-w"), true,
                       nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-X", "apply_local_source_mark", "-w"), true,
                       nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "OUTPUT", "-j",
                                            "apply_local_source_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner,
              ip6tables(StrEq("mangle"),
                        ElementsAre("-F", "apply_local_source_mark", "-w"),
                        true, nullptr));
  EXPECT_CALL(runner,
              ip6tables(StrEq("mangle"),
                        ElementsAre("-X", "apply_local_source_mark", "-w"),
                        true, nullptr));
  // Asserts for apply_vpn_mark chain
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "OUTPUT", "-m", "mark",
                                           "--mark", "0x00008000/0x0000c000",
                                           "-j", "apply_vpn_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-F", "apply_vpn_mark", "-w"), true,
                               nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-X", "apply_vpn_mark", "-w"), true,
                               nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "OUTPUT", "-m", "mark",
                                            "--mark", "0x00008000/0x0000c000",
                                            "-j", "apply_vpn_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-F", "apply_vpn_mark", "-w"), true,
                                nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-X", "apply_vpn_mark", "-w"), true,
                                nullptr));
  // Asserts for check_routing_mark chain
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-j",
                                           "CONNMARK", "--restore-mark",
                                           "--mask", "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-m", "mark",
                                           "!", "--mark", "0x0/0xffff0000",
                                           "-j", "check_routing_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-F", "check_routing_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-X", "check_routing_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-j",
                                            "CONNMARK", "--restore-mark",
                                            "--mask", "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-m", "mark",
                                            "!", "--mark", "0x0/0xffff0000",
                                            "-j", "check_routing_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-F", "check_routing_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-X", "check_routing_mark", "-w"),
                                true, nullptr));

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
  EXPECT_CALL(runner, ip(StrEq("tuntap"), StrEq("del"),
                         ElementsAre("foo0", "mode", "tap"), true));
  Datapath datapath(&runner, &firewall);
  datapath.RemoveTAP("foo0");
}

TEST(DatapathTest, NetnsAttachName) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, ip_netns_delete(StrEq("netns_foo"), false));
  EXPECT_CALL(runner, ip_netns_attach(StrEq("netns_foo"), 1234, true));
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
  EXPECT_CALL(runner, brctl(StrEq("addbr"), ElementsAre("br"), true));
  EXPECT_CALL(
      runner,
      ip(StrEq("addr"), StrEq("add"),
         ElementsAre("1.1.1.1/30", "brd", "1.1.1.3", "dev", "br"), true));
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("br", "up"), true));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "br", "-j",
                                           "MARK", "--set-mark", "1/1", "-w"),
                               true, nullptr));
  datapath.AddBridge("br", Ipv4Addr(1, 1, 1, 1), 30);
}

TEST(DatapathTest, ConnectVethPair) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("add"),
                         ElementsAre("veth_foo", "type", "veth", "peer", "name",
                                     "peer_foo", "netns", "netns_foo"),
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
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("veth_foo", "up"), true));
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(datapath.ConnectVethPair(kTestPID, "netns_foo", "veth_foo",
                                       "peer_foo", {1, 2, 3, 4, 5, 6},
                                       Ipv4Addr(100, 115, 92, 169), 30, true));
}

TEST(DatapathTest, AddVirtualInterfacePair) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("add"),
                         ElementsAre("veth_foo", "type", "veth", "peer", "name",
                                     "peer_foo", "netns", "netns_foo"),
                         true));
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(
      datapath.AddVirtualInterfacePair("netns_foo", "veth_foo", "peer_foo"));
}

TEST(DatapathTest, ToggleInterface) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("foo", "up"), true));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("bar", "down"), true));
  Datapath datapath(&runner, &firewall);
  EXPECT_TRUE(datapath.ToggleInterface("foo", true));
  EXPECT_TRUE(datapath.ToggleInterface("bar", false));
}

TEST(DatapathTest, ConfigureInterface) {
  MockProcessRunner runner;
  MockFirewall firewall;
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

  Datapath datapath(&runner, &firewall);
  MacAddress mac_addr = {2, 2, 2, 2, 2, 2};
  EXPECT_TRUE(datapath.ConfigureInterface("foo", mac_addr, Ipv4Addr(1, 1, 1, 1),
                                          30, true, true));
}

TEST(DatapathTest, RemoveInterface) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("delete"), ElementsAre("foo"), false));
  Datapath datapath(&runner, &firewall);
  datapath.RemoveInterface("foo");
}

TEST(DatapathTest, RemoveBridge) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "br", "-j",
                                           "MARK", "--set-mark", "1/1", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("br", "down"), true));
  EXPECT_CALL(runner, brctl(StrEq("delbr"), ElementsAre("br"), true));
  Datapath datapath(&runner, &firewall);
  datapath.RemoveBridge("br");
}

TEST(DatapathTest, AddRemoveSourceIPv4DropRule) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-I", "OUTPUT", "-o", "eth+", "-s",
                                   "100.115.92.0/24", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "OUTPUT", "-o", "eth+", "-s",
                                   "100.115.92.0/24", "-j", "DROP", "-w"),
                       true, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.AddSourceIPv4DropRule("eth+", "100.115.92.0/24");
  datapath.RemoveSourceIPv4DropRule("eth+", "100.115.92.0/24");
}

TEST(DatapathTest, StartRoutingNamespace) {
  MockProcessRunner runner;
  MockFirewall firewall;
  MacAddress mac = {1, 2, 3, 4, 5, 6};

  EXPECT_CALL(runner, ip_netns_delete(StrEq("netns_foo"), false));
  EXPECT_CALL(runner, ip_netns_attach(StrEq("netns_foo"), kTestPID, true));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("add"),
                         ElementsAre("arc_ns0", "type", "veth", "peer", "name",
                                     "veth0", "netns", "netns_foo"),
                         true));
  EXPECT_CALL(runner, ip(StrEq("addr"), StrEq("add"),
                         ElementsAre("100.115.92.130/30", "brd",
                                     "100.115.92.131", "dev", "veth0"),
                         true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("dev", "veth0", "up", "addr",
                                     "01:02:03:04:05:06", "multicast", "off"),
                         true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("arc_ns0", "up"), true));
  EXPECT_CALL(runner, ip(StrEq("addr"), StrEq("add"),
                         ElementsAre("100.115.92.129/30", "brd",
                                     "100.115.92.131", "dev", "arc_ns0"),
                         true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("dev", "arc_ns0", "up", "addr",
                                     "01:02:03:04:05:06", "multicast", "off"),
                         true))
      .WillOnce(Return(0));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-o", "arc_ns0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-i", "arc_ns0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "PREROUTING", "-i", "arc_ns0", "-j",
                                   "MARK", "--set-mark", "1/1", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arc_ns0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00000200/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arc_ns0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "arc_ns0",
                                            "-j", "MARK", "--set-mark",
                                            "0x00000200/0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "arc_ns0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arc_ns0",
                                           "-j", "apply_vpn_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "arc_ns0",
                                            "-j", "apply_vpn_mark", "-w"),
                                true, nullptr));

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

  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-o", "arc_ns0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-i", "arc_ns0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "PREROUTING", "-i", "arc_ns0", "-j",
                                   "MARK", "--set-mark", "1/1", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arc_ns0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00000200/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arc_ns0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "arc_ns0",
                                            "-j", "MARK", "--set-mark",
                                            "0x00000200/0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "arc_ns0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arc_ns0",
                                           "-j", "apply_vpn_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "arc_ns0",
                                            "-j", "apply_vpn_mark", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip_netns_delete(StrEq("netns_foo"), true));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("delete"), ElementsAre("arc_ns0"),
                         false));

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
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-i", "eth0", "-o",
                                           "arc_eth0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-i", "arc_eth0",
                                           "-o", "eth0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arc_eth0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00002000/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arc_eth0",
                                           "-j", "MARK", "--set-mark",
                                           "0x03ea0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "PREROUTING", "-i", "arc_eth0", "-j", "MARK",
                            "--set-mark", "0x00002000/0x00003f00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-A", "PREROUTING", "-i", "arc_eth0", "-j", "MARK",
                            "--set-mark", "0x03ea0000/0xffff0000", "-w"),
                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("eth0", 2);
  datapath.StartRoutingDevice("eth0", "arc_eth0", Ipv4Addr(1, 2, 3, 4),
                              TrafficSource::ARC, false);
}

TEST(DatapathTest, StartRoutingDevice_CrosVM) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-o", "vmtap0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-i", "vmtap0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "vmtap0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00002100/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "vmtap0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "vmtap0",
                                            "-j", "MARK", "--set-mark",
                                            "0x00002100/0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "vmtap0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "vmtap0",
                                           "-j", "apply_vpn_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "vmtap0",
                                            "-j", "apply_vpn_mark", "-w"),
                                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.StartRoutingDevice("", "vmtap0", Ipv4Addr(1, 2, 3, 4),
                              TrafficSource::CROSVM, true);
}

TEST(DatapathTest, StopRoutingDevice_Arc) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-i", "eth0", "-o",
                                           "arc_eth0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-i", "arc_eth0",
                                           "-o", "eth0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arc_eth0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00002000/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arc_eth0",
                                           "-j", "MARK", "--set-mark",
                                           "0x03ea0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-D", "PREROUTING", "-i", "arc_eth0", "-j", "MARK",
                            "--set-mark", "0x00002000/0x00003f00", "-w"),
                true, nullptr));
  EXPECT_CALL(
      runner,
      ip6tables(StrEq("mangle"),
                ElementsAre("-D", "PREROUTING", "-i", "arc_eth0", "-j", "MARK",
                            "--set-mark", "0x03ea0000/0xffff0000", "-w"),
                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("eth0", 2);
  datapath.StopRoutingDevice("eth0", "arc_eth0", Ipv4Addr(1, 2, 3, 4),
                             TrafficSource::ARC, true);
}

TEST(DatapathTest, StopRoutingDevice_CrosVM) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-o", "vmtap0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-i", "vmtap0",
                                           "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "vmtap0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00002100/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "vmtap0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "vmtap0",
                                            "-j", "MARK", "--set-mark",
                                            "0x00002100/0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "vmtap0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "vmtap0",
                                           "-j", "apply_vpn_mark", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "vmtap0",
                                            "-j", "apply_vpn_mark", "-w"),
                                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.StopRoutingDevice("", "vmtap0", Ipv4Addr(1, 2, 3, 4),
                             TrafficSource::CROSVM, true);
}

TEST(DatapathTest, StartStopIpForwarding) {
  struct {
    IpFamily family;
    std::string iif;
    std::string oif;
    std::vector<std::string> start_args;
    std::vector<std::string> stop_args;
    bool result;
  } testcases[] = {
      {IpFamily::IPv4, "", "", {}, {}, false},
      {IpFamily::NONE, "foo", "bar", {}, {}, false},
      {IpFamily::IPv4,
       "foo",
       "bar",
       {"-A", "FORWARD", "-i", "foo", "-o", "bar", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-i", "foo", "-o", "bar", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::IPv4,
       "",
       "bar",
       {"-A", "FORWARD", "-o", "bar", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-o", "bar", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::IPv4,
       "foo",
       "",
       {"-A", "FORWARD", "-i", "foo", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-i", "foo", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::IPv6,
       "foo",
       "bar",
       {"-A", "FORWARD", "-i", "foo", "-o", "bar", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-i", "foo", "-o", "bar", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::IPv6,
       "",
       "bar",
       {"-A", "FORWARD", "-o", "bar", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-o", "bar", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::IPv6,
       "foo",
       "",
       {"-A", "FORWARD", "-i", "foo", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-i", "foo", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::Dual,
       "foo",
       "bar",
       {"-A", "FORWARD", "-i", "foo", "-o", "bar", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-i", "foo", "-o", "bar", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::Dual,
       "",
       "bar",
       {"-A", "FORWARD", "-o", "bar", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-o", "bar", "-j", "ACCEPT", "-w"},
       true},
      {IpFamily::Dual,
       "foo",
       "",
       {"-A", "FORWARD", "-i", "foo", "-j", "ACCEPT", "-w"},
       {"-D", "FORWARD", "-i", "foo", "-j", "ACCEPT", "-w"},
       true},
  };

  for (const auto& tt : testcases) {
    MockProcessRunner runner;
    MockFirewall firewall;
    if (tt.result) {
      if (tt.family & IpFamily::IPv4) {
        EXPECT_CALL(runner,
                    iptables(StrEq("filter"), tt.start_args, true, nullptr))
            .WillOnce(Return(0));
        EXPECT_CALL(runner,
                    iptables(StrEq("filter"), tt.stop_args, true, nullptr))
            .WillOnce(Return(0));
      }
      if (tt.family & IpFamily::IPv6) {
        EXPECT_CALL(runner,
                    ip6tables(StrEq("filter"), tt.start_args, true, nullptr))
            .WillOnce(Return(0));
        EXPECT_CALL(runner,
                    ip6tables(StrEq("filter"), tt.stop_args, true, nullptr))
            .WillOnce(Return(0));
      }
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
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "check_routing_mark", "-o", "eth0",
                                   "-m", "mark", "!", "--mark",
                                   "0x03eb0000/0xffff0000", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "check_routing_mark", "-o",
                                            "eth0", "-m", "mark", "!", "--mark",
                                            "0x03eb0000/0xffff0000", "-j",
                                            "DROP", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-o", "eth0",
                                           "-j", "CONNMARK", "--set-mark",
                                           "0x03eb0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-o", "eth0",
                                            "-j", "CONNMARK", "--set-mark",
                                            "0x03eb0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-o", "eth0",
                                           "-j", "CONNMARK", "--save-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-o", "eth0",
                                            "-j", "CONNMARK", "--save-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));

  // Teardown
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "check_routing_mark", "-o", "eth0",
                                   "-m", "mark", "!", "--mark",
                                   "0x03eb0000/0xffff0000", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "check_routing_mark", "-o",
                                            "eth0", "-m", "mark", "!", "--mark",
                                            "0x03eb0000/0xffff0000", "-j",
                                            "DROP", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-o", "eth0",
                                           "-j", "CONNMARK", "--set-mark",
                                           "0x03eb0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-o", "eth0",
                                            "-j", "CONNMARK", "--set-mark",
                                            "0x03eb0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-o", "eth0",
                                           "-j", "CONNMARK", "--save-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-o", "eth0",
                                            "-j", "CONNMARK", "--save-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("eth0", 3);
  datapath.StartConnectionPinning("eth0");
  datapath.StopConnectionPinning("eth0");
}

TEST(DatapathTest, StartStopVpnRouting_ArcVpn) {
  MockProcessRunner runner;
  MockFirewall firewall;

  // Setup
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "check_routing_mark", "-o", "arcbr0",
                                   "-m", "mark", "!", "--mark",
                                   "0x03ed0000/0xffff0000", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "check_routing_mark", "-o",
                                            "arcbr0", "-m", "mark", "!",
                                            "--mark", "0x03ed0000/0xffff0000",
                                            "-j", "DROP", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-o", "arcbr0",
                                           "-j", "CONNMARK", "--set-mark",
                                           "0x03ed0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "apply_vpn_mark", "-j", "MARK",
                                   "--set-mark", "0x03ed0000/0xffff0000", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-o", "arcbr0",
                                            "-j", "CONNMARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "apply_vpn_mark", "-j",
                                            "MARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-o", "arcbr0",
                                           "-j", "CONNMARK", "--save-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-o", "arcbr0",
                                            "-j", "CONNMARK", "--save-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arcbr0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "arcbr0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "POSTROUTING", "-o", "arcbr0",
                                           "-j", "MASQUERADE", "-w"),
                               true, nullptr));
  // Teardown
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "check_routing_mark", "-o", "arcbr0",
                                   "-m", "mark", "!", "--mark",
                                   "0x03ed0000/0xffff0000", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "check_routing_mark", "-o",
                                            "arcbr0", "-m", "mark", "!",
                                            "--mark", "0x03ed0000/0xffff0000",
                                            "-j", "DROP", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-o", "arcbr0",
                                           "-j", "CONNMARK", "--set-mark",
                                           "0x03ed0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "apply_vpn_mark", "-j", "MARK",
                                   "--set-mark", "0x03ed0000/0xffff0000", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-o", "arcbr0",
                                            "-j", "CONNMARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "apply_vpn_mark", "-j",
                                            "MARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-o", "arcbr0",
                                           "-j", "CONNMARK", "--save-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-o", "arcbr0",
                                            "-j", "CONNMARK", "--save-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arcbr0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "arcbr0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "POSTROUTING", "-o", "arcbr0",
                                           "-j", "MASQUERADE", "-w"),
                               true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("arcbr0", 5);
  datapath.StartVpnRouting("arcbr0");
  datapath.StopVpnRouting("arcbr0");
}

TEST(DatapathTest, StartStopVpnRouting_HostVpn) {
  MockProcessRunner runner;
  MockFirewall firewall;

  // Setup
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "check_routing_mark", "-o", "tun0",
                                   "-m", "mark", "!", "--mark",

                                   "0x03ed0000/0xffff0000", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "check_routing_mark", "-o",
                                            "tun0", "-m", "mark", "!", "--mark",

                                            "0x03ed0000/0xffff0000", "-j",
                                            "DROP", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-o", "tun0",
                                           "-j", "CONNMARK", "--set-mark",
                                           "0x03ed0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-A", "apply_vpn_mark", "-j", "MARK",
                                   "--set-mark", "0x03ed0000/0xffff0000", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-o", "tun0",
                                            "-j", "CONNMARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "apply_vpn_mark", "-j",
                                            "MARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "POSTROUTING", "-o", "tun0",
                                           "-j", "CONNMARK", "--save-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "POSTROUTING", "-o", "tun0",
                                            "-j", "CONNMARK", "--save-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "tun0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "tun0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "POSTROUTING", "-o", "tun0",
                                           "-j", "MASQUERADE", "-w"),
                               true, nullptr));
  // Teardown
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "check_routing_mark", "-o", "tun0",
                                   "-m", "mark", "!", "--mark",

                                   "0x03ed0000/0xffff0000", "-j", "DROP", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "check_routing_mark", "-o",
                                            "tun0", "-m", "mark", "!", "--mark",

                                            "0x03ed0000/0xffff0000", "-j",
                                            "DROP", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-o", "tun0",
                                           "-j", "CONNMARK", "--set-mark",
                                           "0x03ed0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("mangle"),
                       ElementsAre("-D", "apply_vpn_mark", "-j", "MARK",
                                   "--set-mark", "0x03ed0000/0xffff0000", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-o", "tun0",
                                            "-j", "CONNMARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "apply_vpn_mark", "-j",
                                            "MARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "POSTROUTING", "-o", "tun0",
                                           "-j", "CONNMARK", "--save-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "POSTROUTING", "-o", "tun0",
                                            "-j", "CONNMARK", "--save-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "tun0",
                                           "-j", "CONNMARK", "--restore-mark",
                                           "--mask", "0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "tun0",
                                            "-j", "CONNMARK", "--restore-mark",
                                            "--mask", "0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "POSTROUTING", "-o", "tun0",
                                           "-j", "MASQUERADE", "-w"),
                               true, nullptr));
  // Start tun0 <-> arcbr0 routing
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-i", "tun0", "-o",
                                           "arcbr0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-i", "arcbr0",
                                           "-o", "tun0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arcbr0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00002000/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "arcbr0",
                                           "-j", "MARK", "--set-mark",
                                           "0x03ed0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "arcbr0",
                                            "-j", "MARK", "--set-mark",
                                            "0x00002000/0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-A", "PREROUTING", "-i", "arcbr0",
                                            "-j", "MARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));
  // Stop tun0 <-> arcbr0 routing
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-i", "tun0", "-o",
                                           "arcbr0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-i", "arcbr0",
                                           "-o", "tun0", "-j", "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arcbr0",
                                           "-j", "MARK", "--set-mark",
                                           "0x00002000/0x00003f00", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "arcbr0",
                                           "-j", "MARK", "--set-mark",
                                           "0x03ed0000/0xffff0000", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "arcbr0",
                                            "-j", "MARK", "--set-mark",
                                            "0x00002000/0x00003f00", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("mangle"),
                                ElementsAre("-D", "PREROUTING", "-i", "arcbr0",
                                            "-j", "MARK", "--set-mark",
                                            "0x03ed0000/0xffff0000", "-w"),
                                true, nullptr));

  Datapath datapath(&runner, &firewall);
  datapath.SetIfnameIndex("tun0", 5);
  datapath.StartVpnRouting("tun0");
  datapath.StopVpnRouting("tun0");
}

TEST(DatapathTest, AddInboundIPv4DNAT) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.AddInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, RemoveInboundIPv4DNAT) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true, nullptr));
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
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-D", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                true, nullptr));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-D", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                true, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.RemoveIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6HostRoute) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(runner,
              ip6(StrEq("route"), StrEq("replace"),
                  ElementsAre("2001:da8:e00::1234/128", "dev", "eth0"), true));
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

TEST(DatapathTest, AddSNATMarkRules) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(
      runner,
      iptables(StrEq("filter"),
               ElementsAre("-A", "FORWARD", "-m", "mark", "--mark", "1/1", "-m",
                           "state", "--state", "INVALID", "-j", "DROP", "-w"),
               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-A", "FORWARD", "-m", "mark", "--mark",
                                   "1/1", "-j", "ACCEPT", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-A", "POSTROUTING", "-m", "mark", "--mark",
                                   "1/1", "-j", "MASQUERADE", "-w"),
                       true, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.AddSNATMarkRules();
}

TEST(DatapathTest, RemoveSNATMarkRules) {
  MockProcessRunner runner;
  MockFirewall firewall;
  EXPECT_CALL(
      runner,
      iptables(StrEq("filter"),
               ElementsAre("-D", "FORWARD", "-m", "mark", "--mark", "1/1", "-m",
                           "state", "--state", "INVALID", "-j", "DROP", "-w"),
               true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("filter"),
                       ElementsAre("-D", "FORWARD", "-m", "mark", "--mark",
                                   "1/1", "-j", "ACCEPT", "-w"),
                       true, nullptr));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-D", "POSTROUTING", "-m", "mark", "--mark",
                                   "1/1", "-j", "MASQUERADE", "-w"),
                       true, nullptr));
  Datapath datapath(&runner, &firewall);
  datapath.RemoveSNATMarkRules();
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

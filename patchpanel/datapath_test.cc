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

#include <base/containers/contains.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_util.h>
#include <chromeos/net-base/mac_address.h>
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
using testing::StrEq;
using testing::UnorderedElementsAreArray;

namespace patchpanel {
namespace {

// TODO(hugobenichi) Centralize this constant definition
constexpr pid_t kTestPID = -2;

// This class fakes the implementation for iptables() and ip6tables() based on a
// MockProcessRunner. For these two functions:
// - This class will record the calls to these two functions;
// - The test can call `AddIptablesExpectation()` to add an expectation (similar
//   to EXPECT_CALL).
// - The test can call `VerifyAndClearIptablesExpectations()` to verify the
//   expectations. It will also be called automatically in the destructor so we
//   won't miss any iptables calls.
//
// The main reason we need to do this instead of using EXPECT_CALL() is that
// when there are a large number of calls to the mocked function, the error
// message is hard to read when there is an error, while the error messages from
// `ElementsAreArray()` are much better.
class MockProcessRunnerForIptablesTest : public MockProcessRunner {
 public:
  MockProcessRunnerForIptablesTest() = default;

  ~MockProcessRunnerForIptablesTest() = default;

  void UseIptablesSeccompFilter(minijail* jail) override {}
};

class MockFirewall : public Firewall {
 public:
  MockFirewall() = default;
  ~MockFirewall() override = default;

  MOCK_METHOD3(AddAcceptRules,
               bool(patchpanel::ModifyPortRuleRequest::Protocol protocol,
                    uint16_t port,
                    std::string_view interface));
  MOCK_METHOD3(DeleteAcceptRules,
               bool(Protocol protocol,
                    uint16_t port,
                    std::string_view interface));
  MOCK_METHOD2(AddLoopbackLockdownRules,
               bool(Protocol protocol, uint16_t port));
  MOCK_METHOD2(DeleteLoopbackLockdownRules,
               bool(Protocol protocol, uint16_t port));
  MOCK_METHOD6(AddIpv4ForwardRule,
               bool(Protocol protocol,
                    const std::optional<net_base::IPv4Address>& input_ip,
                    uint16_t port,
                    std::string_view interface,
                    const net_base::IPv4Address& dst_ip,
                    uint16_t dst_port));
  MOCK_METHOD6(DeleteIpv4ForwardRule,
               bool(Protocol protocol,
                    const std::optional<net_base::IPv4Address>& input_ip,
                    uint16_t port,
                    std::string_view interface,
                    const net_base::IPv4Address& dst_ip,
                    uint16_t dst_port));
};

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
  DatapathTest() : firewall_(new MockFirewall()) {
    ExpectForConstructor();
    datapath_ = std::make_unique<Datapath>(&runner_, firewall_, &system_);

    VerifyAndClearExpectations();  // Verify the expectations of the ctor.
  }

  ~DatapathTest() override {
    VerifyAndClearExpectations();  // Verify the expectations of test cases.

    ExpectForDestructor();
    datapath_.reset();
    VerifyAndClearExpectations();  // Verify the expectations of the dtor.
  }

  // Sets the expectataion for Datapath's constructor.
  void ExpectForConstructor() {
    EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv4Forward, "1", ""));
    EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6Forward, "1", ""));
    EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6ProxyNDP, "1", ""));

    static struct {
      IpFamily family;
      std::string args;
      int call_count = 1;
    } iptables_commands[] = {
        {IpFamily::kDual,
         "mangle -A qos_detect -m mark ! --mark 0x00000000/0x000000e0 -j "
         "RETURN "
         "-w"},
        {IpFamily::kDual,
         "mangle -A qos_detect -m bpf --object-pinned "
         "/sys/fs/bpf/patchpanel/match_dtls_srtp -j MARK --set-xmark "
         "0x00000040/0x000000e0 -w"},
        {IpFamily::kDual,
         "mangle -A qos_detect -j CONNMARK --save-mark --nfmask 0x000000e0 "
         "--ctmask 0x000000e0 -w"},
    };
    for (const auto& c : iptables_commands) {
      runner_.ExpectCallIptables(c.family, c.args, c.call_count);
    }
  }

  // Sets the expectataion for Datapath's destructor.
  void ExpectForDestructor() {
    EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv4Forward, "0", ""));
    EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6Forward, "0", ""));
  }

  // Verifies and clears the expectations for all the mock instances.
  void VerifyAndClearExpectations() {
    testing::Mock::VerifyAndClearExpectations(&system_);
    testing::Mock::VerifyAndClearExpectations(&runner_);
    testing::Mock::VerifyAndClearExpectations(firewall_);
  }

  FakeSystem system_;
  MockProcessRunnerForIptablesTest runner_;
  MockFirewall* firewall_;

  std::unique_ptr<Datapath> datapath_;
};

TEST_F(DatapathTest, AddTUN) {
  constexpr net_base::MacAddress mac(1, 2, 3, 4, 5, 6);
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  const std::string ifname = datapath_->AddTunTap(
      "foo0", mac, subnet.CIDRAtOffset(1), "", DeviceMode::kTun);

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
  auto ifname = datapath_->AddTunTap(
      "foo0", std::nullopt, subnet.CIDRAtOffset(1), "", DeviceMode::kTun);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {TUNSETIFF,    TUNSETPERSIST,
                                       SIOCSIFADDR,  SIOCSIFNETMASK,
                                       SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, RemoveTUN) {
  runner_.ExpectCallIp(IpFamily::kIPv4, "tuntap del foo0 mode tun");

  datapath_->RemoveTunTap("foo0", DeviceMode::kTun);
}

TEST_F(DatapathTest, AddTAP) {
  constexpr net_base::MacAddress mac(1, 2, 3, 4, 5, 6);
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  const std::string ifname = datapath_->AddTunTap(
      "foo0", mac, subnet.CIDRAtOffset(1), "", DeviceMode::kTap);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,     TUNSETPERSIST, SIOCSIFADDR, SIOCSIFNETMASK,
      SIOCSIFHWADDR, SIOCGIFFLAGS,  SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddTAPWithOwner) {
  constexpr net_base::MacAddress mac(1, 2, 3, 4, 5, 6);
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({100, 115, 92, 4}, 30),
      base::DoNothing());
  const std::string ifname = datapath_->AddTunTap(
      "foo0", mac, subnet.CIDRAtOffset(1), "root", DeviceMode::kTap);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {
      TUNSETIFF,      TUNSETPERSIST, TUNSETOWNER,  SIOCSIFADDR,
      SIOCSIFNETMASK, SIOCSIFHWADDR, SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddTAPNoAddrs) {
  auto ifname = datapath_->AddTunTap("foo0", std::nullopt, std::nullopt, "",
                                     DeviceMode::kTap);

  EXPECT_EQ(ifname, "foo0");
  std::vector<ioctl_req_t> expected = {TUNSETIFF, TUNSETPERSIST, SIOCGIFFLAGS,
                                       SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, RemoveTAP) {
  runner_.ExpectCallIp(IpFamily::kIPv4, "tuntap del foo0 mode tap");

  datapath_->RemoveTunTap("foo0", DeviceMode::kTap);
}

TEST_F(DatapathTest, NetnsAttachName) {
  Verify_ip_netns_delete(runner_, "netns_foo");
  Verify_ip_netns_attach(runner_, "netns_foo", 1234);

  EXPECT_TRUE(datapath_->NetnsAttachName("netns_foo", 1234));
}

TEST_F(DatapathTest, NetnsDeleteName) {
  EXPECT_CALL(runner_, ip_netns_delete(StrEq("netns_foo"), true));

  EXPECT_TRUE(datapath_->NetnsDeleteName("netns_foo"));
}

TEST_F(DatapathTest, AddBridge) {
  runner_.ExpectCallIp(IpFamily::kIPv4,
                       "addr add 1.1.1.1/30 brd 1.1.1.3 dev br");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set br up");

  datapath_->AddBridge("br", *IPv4CIDR::CreateFromCIDRString("1.1.1.1/30"));

  EXPECT_EQ(1, system_.ioctl_reqs.size());
  EXPECT_EQ(SIOCBRADDBR, system_.ioctl_reqs[0]);
  EXPECT_EQ("br", system_.ioctl_ifreq_args[0].first);
}

TEST_F(DatapathTest, RemoveBridge) {
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set br down");

  datapath_->RemoveBridge("br");

  EXPECT_EQ(1, system_.ioctl_reqs.size());
  EXPECT_EQ(SIOCBRDELBR, system_.ioctl_reqs[0]);
  EXPECT_EQ("br", system_.ioctl_ifreq_args[0].first);
}

TEST_F(DatapathTest, AddToBridge) {
  EXPECT_CALL(system_, IfNametoindex("vethwlan0")).WillRepeatedly(Return(5));

  datapath_->AddToBridge("arcbr0", "vethwlan0");

  EXPECT_EQ(1, system_.ioctl_reqs.size());
  EXPECT_EQ(SIOCBRADDIF, system_.ioctl_reqs[0]);
  EXPECT_EQ("arcbr0", system_.ioctl_ifreq_args[0].first);
  EXPECT_EQ(5, system_.ioctl_ifreq_args[0].second.ifr_ifindex);
}

TEST_F(DatapathTest, ConnectVethPair) {
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link add veth_foo type veth peer name peer_foo netns netns_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.169/30 brd 100.115.92.171 dev peer_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev peer_foo up addr 01:02:03:04:05:06 multicast on");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set veth_foo up");

  EXPECT_TRUE(datapath_->ConnectVethPair(
      kTestPID, "netns_foo", "veth_foo", "peer_foo", {1, 2, 3, 4, 5, 6},
      *IPv4CIDR::CreateFromCIDRString("100.115.92.169/30"),
      /*remote_ipv6_cidr=*/std::nullopt, true));
}

TEST_F(DatapathTest, ConnectVethPair_StaticIPv6) {
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link add veth_foo type veth peer name peer_foo netns netns_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.169/30 brd 100.115.92.171 dev peer_foo");
  runner_.ExpectCallIp(IpFamily::kIPv4, "addr add fd11::1234/64 dev peer_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev peer_foo up addr 01:02:03:04:05:06 multicast on");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set veth_foo up");

  EXPECT_TRUE(datapath_->ConnectVethPair(
      kTestPID, "netns_foo", "veth_foo", "peer_foo", {1, 2, 3, 4, 5, 6},
      *IPv4CIDR::CreateFromCIDRString("100.115.92.169/30"),
      *IPv6CIDR::CreateFromCIDRString("fd11::1234/64"), true));
}

TEST_F(DatapathTest, ConnectVethPair_InterfaceDown) {
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link add veth_foo type veth peer name peer_foo netns netns_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.169/30 brd 100.115.92.171 dev peer_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev peer_foo down addr 01:02:03:04:05:06 multicast on");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set veth_foo up");

  EXPECT_TRUE(datapath_->ConnectVethPair(
      kTestPID, "netns_foo", "veth_foo", "peer_foo", {1, 2, 3, 4, 5, 6},
      *IPv4CIDR::CreateFromCIDRString("100.115.92.169/30"),
      /*remote_ipv6_cidr=*/std::nullopt, true, /*up=*/false));
}

TEST_F(DatapathTest, AddVirtualInterfacePair) {
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link add veth_foo type veth peer name peer_foo netns netns_foo");

  EXPECT_TRUE(
      datapath_->AddVirtualInterfacePair("netns_foo", "veth_foo", "peer_foo"));
}

TEST_F(DatapathTest, ToggleInterface) {
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set foo up");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set bar down");

  EXPECT_TRUE(datapath_->ToggleInterface("foo", true));
  EXPECT_TRUE(datapath_->ToggleInterface("bar", false));
}

TEST_F(DatapathTest, ConfigureInterface) {
  runner_.ExpectCallIp(IpFamily::kIPv4,
                       "addr add 100.115.92.2/30 brd 100.115.92.3 dev test0");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev test0 up addr 02:02:02:02:02:03 multicast on");
  constexpr net_base::MacAddress mac_addr(2, 2, 2, 2, 2, 3);
  EXPECT_TRUE(datapath_->ConfigureInterface(
      "test0", mac_addr, *IPv4CIDR::CreateFromCIDRString("100.115.92.2/30"),
      /*ipv6_cidr=*/std::nullopt, true, true));
  Mock::VerifyAndClearExpectations(&runner_);

  runner_.ExpectCallIp(IpFamily::kIPv4,
                       "addr add 192.168.1.37/24 brd 192.168.1.255 dev test1");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set dev test1 up multicast off");
  EXPECT_TRUE(datapath_->ConfigureInterface(
      "test1", std::nullopt, *IPv4CIDR::CreateFromCIDRString("192.168.1.37/24"),
      /*ipv6_cidr=*/std::nullopt, true, false));
}

TEST_F(DatapathTest, RemoveInterface) {
  runner_.ExpectCallIp(IpFamily::kIPv4, "link delete foo");

  datapath_->RemoveInterface("foo");
}

TEST_F(DatapathTest, StartRoutingNamespace) {
  constexpr net_base::MacAddress peer_mac(1, 2, 3, 4, 5, 6);
  constexpr net_base::MacAddress host_mac(6, 5, 4, 3, 2, 1);

  Verify_ip_netns_delete(runner_, "netns_foo");
  Verify_ip_netns_attach(runner_, "netns_foo", kTestPID);
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link add arc_ns0 type veth peer name veth0 netns netns_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.130/30 brd 100.115.92.131 dev veth0");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev veth0 up addr 01:02:03:04:05:06 multicast off");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set arc_ns0 up");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.129/30 brd 100.115.92.131 dev arc_ns0");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev arc_ns0 up addr 06:05:04:03:02:01 multicast off");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -o arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -i arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -N PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                             "0x00000200/0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j CONNMARK "
                             "--restore-mark --mask 0xffff0000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "mangle -A PREROUTING_arc_ns0 -s 100.115.92.130 -d "
      "100.115.92.129 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                             "0x00008000/0x0000c000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A PREROUTING_arc_ns0 -j apply_vpn_mark -w");

  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv4DefaultTTL, "65", ""));
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kIPv6HopLimit, "65", "veth0"));

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

  datapath_->StartRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StopRoutingNamespace) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -o arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -i arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -X PREROUTING_arc_ns0 -w");
  Verify_ip_netns_delete(runner_, "netns_foo");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link delete arc_ns0");

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

  datapath_->StopRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StartRoutingNamespace_StaticIPv6) {
  constexpr net_base::MacAddress peer_mac(1, 2, 3, 4, 5, 6);
  constexpr net_base::MacAddress host_mac(6, 5, 4, 3, 2, 1);

  Verify_ip_netns_delete(runner_, "netns_foo");
  Verify_ip_netns_attach(runner_, "netns_foo", kTestPID);
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link add arc_ns0 type veth peer name veth0 netns netns_foo");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.130/30 brd 100.115.92.131 dev veth0");
  runner_.ExpectCallIp(IpFamily::kIPv4, "addr add fd11::2/64 dev veth0");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev veth0 up addr 01:02:03:04:05:06 multicast off");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link set arc_ns0 up");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "addr add 100.115.92.129/30 brd 100.115.92.131 dev arc_ns0");
  runner_.ExpectCallIp(IpFamily::kIPv4, "addr add fd11::1/64 dev arc_ns0");
  runner_.ExpectCallIp(
      IpFamily::kIPv4,
      "link set dev arc_ns0 up addr 06:05:04:03:02:01 multicast off");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -o arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -i arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -N PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                             "0x00000200/0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j CONNMARK "
                             "--restore-mark --mask 0xffff0000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "mangle -A PREROUTING_arc_ns0 -s 100.115.92.130 -d "
      "100.115.92.129 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "mangle -A PREROUTING_arc_ns0 -s fd11::2 -d "
                             "fd11::1 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_ns0 -j MARK --set-mark "
                             "0x00008000/0x0000c000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A PREROUTING_arc_ns0 -j apply_vpn_mark -w");

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

  datapath_->StartRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StopRoutingNamespace_StaticIPv6) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -o arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -i arc_ns0 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D PREROUTING -i arc_ns0 -j PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F PREROUTING_arc_ns0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -X PREROUTING_arc_ns0 -w");
  Verify_ip_netns_delete(runner_, "netns_foo");
  runner_.ExpectCallIp(IpFamily::kIPv4, "link delete arc_ns0");

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

  datapath_->StopRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StartDownstreamTetheredNetwork) {
  EXPECT_CALL(system_, IfNametoindex("wwan0")).WillRepeatedly(Return(4));
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -I OUTPUT -o ap0 -j egress_tethering -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_downstream_network -i ap0 -j ingress_tethering -w");

  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -A forward_tethering -i wwan0 -o ap0 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -A forward_tethering -i ap0 -o wwan0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A forward_tethering -o ap0 -j DROP -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A forward_tethering -i ap0 -j DROP -w");

  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -N PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A PREROUTING -i ap0 -j PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                             "0x00002300/0x00003f00 -w",
                             /*call_times=*/2);
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                             "0x03ec0000/0xffff0000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING_ap0 -j CONNMARK --restore-mark "
      "--mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_ap0 -m mark ! --mark "
                             "0x00000000/0x00003f00 -j RETURN -w");

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kTethering;
  info.upstream_device = ShillClient::Device();
  info.upstream_device->ifname = "wwan0";
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_->StartDownstreamNetwork(info);
}

TEST_F(DatapathTest, StartDownstreamLocalOnlyNetwork) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -I OUTPUT -o ap0 -j egress_localonly -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -I ingress_downstream_network -i ap0 -j ingress_localonly -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A forward_localonly -o ap0 -j DROP -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A forward_localonly -i ap0 -j DROP -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -N PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A PREROUTING -i ap0 -j PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_ap0 -j MARK --set-mark "
                             "0x00002700/0x00003f00 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING_ap0 -j CONNMARK --restore-mark "
      "--mask 0xffff0000 -w");

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kLocalOnly;
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_->StartDownstreamNetwork(info);
}

TEST_F(DatapathTest, StopDownstreamTetheredNetwork) {
  runner_.ExpectCallIptables(IpFamily::kDual, "filter -F forward_tethering -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -D PREROUTING -i ap0 -j PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -X PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D OUTPUT -o ap0 -j egress_tethering -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -D ingress_downstream_network -i ap0 -j ingress_tethering -w");
  runner_.ExpectNoCallIp();

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kTethering;
  info.upstream_device = ShillClient::Device();
  info.upstream_device->ifname = "wwan0";
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_->StopDownstreamNetwork(info);
}

TEST_F(DatapathTest, StopDownstreamLocalOnlyNetwork) {
  runner_.ExpectCallIptables(IpFamily::kDual, "filter -F forward_localonly -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -D PREROUTING -i ap0 -j PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -X PREROUTING_ap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D OUTPUT -o ap0 -j egress_localonly -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -D ingress_downstream_network -i ap0 -j ingress_localonly -w");
  runner_.ExpectNoCallIp();

  DownstreamNetworkInfo info;
  info.topology = DownstreamNetworkTopology::kLocalOnly;
  info.downstream_ifname = "ap0";
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("172.17.49.1/24");
  info.enable_ipv4_dhcp = true;

  datapath_->StopDownstreamNetwork(info);
}

TEST_F(DatapathTest, StartRoutingNewNamespace) {
  constexpr net_base::MacAddress mac(1, 2, 3, 4, 5, 6);

  // The running may fail at checking ScopedNS.IsValid() in
  // Datapath::ConnectVethPair(), so we only check if `ip netns add` is invoked
  // correctly here.
  Verify_ip_netns_add(runner_, "netns_foo");

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

  datapath_->StartRoutingNamespace(nsinfo);
}

TEST_F(DatapathTest, StartRoutingDevice) {
  EXPECT_CALL(system_, IfNametoindex("eth0")).WillRepeatedly(Return(2));
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -o arc_eth0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -i arc_eth0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -N PREROUTING_arc_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F PREROUTING_arc_eth0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING -i arc_eth0 -j PREROUTING_arc_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "mangle -A PREROUTING_arc_eth0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_eth0 -j MARK --set-mark "
                             "0x00002000/0x00003f00 -w",
                             /*call_times=*/2);
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_eth0 -j MARK --set-mark "
                             "0x03ea0000/0xffff0000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING_arc_eth0 -j CONNMARK --restore-mark "
      "--mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arc_eth0 -m mark ! --mark "
                             "0x00000000/0x00003f00 -j RETURN -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->StartRoutingDevice(eth_device, "arc_eth0", TrafficSource::kArc);
}

TEST_F(DatapathTest, StartRoutingDeviceAsUser) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -o vmtap0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -i vmtap0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -N PREROUTING_vmtap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_vmtap0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING -i vmtap0 -j PREROUTING_vmtap0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "mangle -A PREROUTING_vmtap0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_vmtap0 -j MARK --set-mark "
                             "0x00002100/0x00003f00 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING_vmtap0 -j CONNMARK --restore-mark "
      "--mask 0xffff0000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A PREROUTING_vmtap0 -j skip_apply_vpn_mark -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_vmtap0 -j MARK --set-mark "
                             "0x00008000/0x0000c000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A PREROUTING_vmtap0 -j apply_vpn_mark -w");

  datapath_->StartRoutingDeviceAsUser("vmtap0", TrafficSource::kCrostiniVM,
                                      IPv4Address(1, 2, 3, 4),
                                      /*int_ipv4_addr=*/std::nullopt);
}

TEST_F(DatapathTest, StopRoutingDevice) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -o arc_eth0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -i arc_eth0 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D PREROUTING -i arc_eth0 -j PREROUTING_arc_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F PREROUTING_arc_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -X PREROUTING_arc_eth0 -w");

  datapath_->StopRoutingDevice("arc_eth0", TrafficSource::kArc);
}

TEST_F(DatapathTest, StopRoutingDeviceAsUser) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -o vmtap0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -i vmtap0 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D PREROUTING -i vmtap0 -j PREROUTING_vmtap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_vmtap0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -X PREROUTING_vmtap0 -w");

  datapath_->StopRoutingDevice("vmtap0", TrafficSource::kCrostiniVM);
}

TEST_F(DatapathTest, StartStopConnectionPinning) {
  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  // Setup
  EXPECT_CALL(system_, IfNametoindex("eth0")).WillRepeatedly(Return(3));
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -N POSTROUTING_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F POSTROUTING_eth0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A POSTROUTING -o eth0 -j POSTROUTING_eth0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A POSTROUTING_eth0 -j CONNMARK --set-mark "
      "0x03eb0000/0xffff0000 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A POSTROUTING_eth0 -j CONNMARK "
                             "--save-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING -i eth0 -j CONNMARK "
                             "--restore-mark --mask 0x00003f00 -w");
  datapath_->StartConnectionPinning(eth_device);

  // Teardown
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F POSTROUTING_eth0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -D POSTROUTING -o eth0 -j POSTROUTING_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -X POSTROUTING_eth0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -D PREROUTING -i eth0 -j CONNMARK "
                             "--restore-mark --mask 0x00003f00 -w");
  datapath_->StopConnectionPinning(eth_device);
}

TEST_F(DatapathTest, StartStopVpnRouting_ArcVpn) {
  ShillClient::Device vpn_device;
  vpn_device.ifname = "arcbr0";

  // Setup
  EXPECT_CALL(system_, IfNametoindex("arcbr0")).WillRepeatedly(Return(5));
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "nat -A POSTROUTING -o arcbr0 -j MASQUERADE -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -N POSTROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F POSTROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A POSTROUTING -o arcbr0 -j POSTROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A POSTROUTING_arcbr0 -j CONNMARK "
                             "--set-mark 0x03ed0000/0xffff0000 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A POSTROUTING_arcbr0 -j CONNMARK "
                             "--save-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING -i arcbr0 -j CONNMARK "
                             "--restore-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A apply_vpn_mark -m mark ! --mark 0x0/0xffff0000 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
      "redirect_dns -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A vpn_accept -m mark "
                             "--mark 0x03ed0000/0xffff0000 -j ACCEPT -w");
  datapath_->StartVpnRouting(vpn_device);

  runner_.ExpectCallIptables(IpFamily::kDual, "filter -F vpn_accept -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F apply_vpn_mark -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D POSTROUTING -o arcbr0 -j POSTROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -F POSTROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -X POSTROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -D PREROUTING -i arcbr0 -j CONNMARK "
                             "--restore-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "nat -D POSTROUTING -o arcbr0 -j MASQUERADE -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
      "redirect_dns -w");
  // Teardown
  datapath_->StopVpnRouting(vpn_device);
}

TEST_F(DatapathTest, StartStopVpnRouting_HostVpn) {
  ShillClient::Device vpn_device;
  vpn_device.ifname = "tun0";

  // Setup
  EXPECT_CALL(system_, IfNametoindex("tun0")).WillRepeatedly(Return(5));
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "nat -A POSTROUTING -o tun0 -j MASQUERADE -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -N POSTROUTING_tun0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F POSTROUTING_tun0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A POSTROUTING -o tun0 -j POSTROUTING_tun0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A POSTROUTING_tun0 -j CONNMARK --set-mark "
      "0x03ed0000/0xffff0000 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A POSTROUTING_tun0 -j CONNMARK "
                             "--save-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING -i tun0 -j CONNMARK "
                             "--restore-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A apply_vpn_mark -m mark ! --mark 0x0/0xffff0000 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A apply_vpn_mark -j MARK --set-mark 0x03ed0000/0xffff0000 -w");
  // Start arcbr0 routing
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -o arcbr0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A FORWARD -i arcbr0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -N PREROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING -i arcbr0 -j PREROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "mangle -A PREROUTING_arcbr0 -j MARK --set-mark "
                             "0x00000001/0x00000001 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arcbr0 -j MARK --set-mark "
                             "0x00002000/0x00003f00 -w",
                             /*call_times=*/2);
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arcbr0 -j MARK --set-mark "
                             "0x03ed0000/0xffff0000 -w");

  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A PREROUTING_arcbr0 -j CONNMARK --restore-mark "
      "--mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A PREROUTING_arcbr0 -m mark ! --mark "
                             "0x00000000/0x00003f00 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
      "redirect_dns -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -A vpn_accept -m mark "
                             "--mark 0x03ed0000/0xffff0000 -j ACCEPT -w");
  datapath_->StartVpnRouting(vpn_device);

  // Teardown
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F POSTROUTING_tun0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -D POSTROUTING -o tun0 -j POSTROUTING_tun0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -X POSTROUTING_tun0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F apply_vpn_mark -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -D PREROUTING -i tun0 -j CONNMARK "
                             "--restore-mark --mask 0x00003f00 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "nat -D POSTROUTING -o tun0 -j MASQUERADE -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D OUTPUT -m mark ! --mark 0x00008000/0x0000c000 -j "
      "redirect_dns -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "filter -F vpn_accept -w");
  // Stop arcbr0 routing
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -o arcbr0 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "filter -D FORWARD -i arcbr0 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D PREROUTING -i arcbr0 -j PREROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -F PREROUTING_arcbr0 -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -X PREROUTING_arcbr0 -w");
  datapath_->StopVpnRouting(vpn_device);
}

TEST_F(DatapathTest, AddInboundIPv4DNATArc) {
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "nat -A apply_auto_dnat_to_arc -i eth0 -m socket "
                             "--nowildcard -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_arc -i eth0 -p tcp -j DNAT "
      "--to-destination 1.2.3.4 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_arc -i eth0 -p udp -j DNAT "
      "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->AddInboundIPv4DNAT(AutoDNATTarget::kArc, eth_device,
                                IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, RemoveInboundIPv4DNATArc) {
  runner_.ExpectCallIptables(IpFamily::kIPv4,
                             "nat -D apply_auto_dnat_to_arc -i eth0 -m socket "
                             "--nowildcard -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_arc -i eth0 -p tcp -j DNAT "
      "--to-destination 1.2.3.4 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_arc -i eth0 -p udp -j DNAT "
      "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->RemoveInboundIPv4DNAT(AutoDNATTarget::kArc, eth_device,
                                   IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, AddInboundIPv4DNATCrostini) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_crostini -i eth0 -m socket "
      "--nowildcard -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_crostini -i eth0 -p tcp -j DNAT "
      "--to-destination 1.2.3.4 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_crostini -i eth0 -p udp -j DNAT "
      "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->AddInboundIPv4DNAT(AutoDNATTarget::kCrostini, eth_device,
                                IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, RemoveInboundIPv4DNATCrostini) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_crostini -i eth0 -m socket "
      "--nowildcard -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_crostini -i eth0 -p tcp -j DNAT "
      "--to-destination 1.2.3.4 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_crostini -i eth0 -p udp -j DNAT "
      "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->RemoveInboundIPv4DNAT(AutoDNATTarget::kCrostini, eth_device,
                                   IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, AddInboundIPv4DNATParallelsVm) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_parallels -i eth0 -m socket "
      "--nowildcard -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_parallels -i eth0 -p tcp -j DNAT "
      "--to-destination 1.2.3.4 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A apply_auto_dnat_to_parallels -i eth0 -p udp -j DNAT "
      "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->AddInboundIPv4DNAT(AutoDNATTarget::kParallels, eth_device,
                                IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, RemoveInboundIPv4DNATParallelsVm) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_parallels -i eth0 -m socket "
      "--nowildcard -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_parallels -i eth0 -p tcp -j DNAT "
      "--to-destination 1.2.3.4 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D apply_auto_dnat_to_parallels -i eth0 -p udp -j DNAT "
      "--to-destination 1.2.3.4 -w");

  ShillClient::Device eth_device;
  eth_device.ifname = "eth0";

  datapath_->RemoveInboundIPv4DNAT(AutoDNATTarget::kParallels, eth_device,
                                   IPv4Address(1, 2, 3, 4));
}

TEST_F(DatapathTest, MaskInterfaceFlags) {
  bool result = datapath_->MaskInterfaceFlags("foo0", IFF_DEBUG);

  EXPECT_TRUE(result);
  std::vector<ioctl_req_t> expected = {SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(system_.ioctl_reqs, expected);
}

TEST_F(DatapathTest, AddIPv6HostRoute) {
  runner_.ExpectCallIp(IpFamily::kIPv6,
                       "route replace 2001:da8:e00::1234/128 dev eth0");

  datapath_->AddIPv6HostRoute("eth0", *net_base::IPv6CIDR::CreateFromCIDRString(
                                          "2001:da8:e00::1234/128"));
}

TEST_F(DatapathTest, AddIPv4RouteToTable) {
  runner_.ExpectCallIp(IpFamily::kIPv4,
                       "route add 192.0.2.2/24 dev eth0 table 123");

  datapath_->AddIPv4RouteToTable(
      "eth0", *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.2/24"), 123);
}

TEST_F(DatapathTest, DeleteIPv4RouteFromTable) {
  runner_.ExpectCallIp(IpFamily::kIPv4,
                       "route del 192.0.2.2/24 dev eth0 table 123");

  datapath_->DeleteIPv4RouteFromTable(
      "eth0", *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.2/24"), 123);
}

TEST_F(DatapathTest, AddIPv4Route) {
  datapath_->AddIPv4Route(IPv4Address(192, 168, 1, 1),
                          *IPv4CIDR::CreateFromCIDRString("100.115.93.0/24"));
  datapath_->DeleteIPv4Route(
      IPv4Address(192, 168, 1, 1),
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

  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_dns -p tcp --dport 53 -o eth0 -j DNAT "
      "--to-destination 192.168.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_dns -p udp --dport 53 -o eth0 -j DNAT "
      "--to-destination 192.168.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 1.1.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 1.1.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 1.1.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 1.1.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 8.8.8.8 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 8.8.8.8 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_dns -p tcp --dport 53 -o eth0 -j DNAT "
      "--to-destination 192.168.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_dns -p udp --dport 53 -o eth0 -j DNAT "
      "--to-destination 192.168.1.1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_dns -p tcp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 8.8.8.8 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_dns -p udp --dport 53 -o wlan0 -j DNAT "
      "--to-destination 8.8.8.8 -w");

  datapath_->RemoveRedirectDnsRule(wlan_device);
  datapath_->RemoveRedirectDnsRule(ShillClient::Device());
  datapath_->AddRedirectDnsRule(eth_device, "192.168.1.1");
  datapath_->AddRedirectDnsRule(wlan_device, "1.1.1.1");
  datapath_->AddRedirectDnsRule(wlan_device, "8.8.8.8");
  datapath_->RemoveRedirectDnsRule(eth_device);
  datapath_->RemoveRedirectDnsRule(wlan_device);
}

// This test needs a mock process runner so it doesn't use Datapath object in
// the fixture class.
TEST_F(DatapathTest, DumpIptables) {
  std::string output = "<iptables output>";
  runner_.ExpectCallIptables(IpFamily::kDual, "mangle -L -x -v -n -w",
                             /*call_times=*/1, output, /*empty_chain*/ true);

  EXPECT_EQ("<iptables output>",
            datapath_->DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle));
  EXPECT_EQ("<iptables output>",
            datapath_->DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle));
  EXPECT_EQ("",
            datapath_->DumpIptables(IpFamily::kDual, Iptables::Table::kMangle));
}

TEST_F(DatapathTest, SetVpnLockdown) {
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "filter -A vpn_lockdown -m mark --mark 0x00008000/0x0000c000 "
      "-j REJECT -w");
  runner_.ExpectCallIptables(IpFamily::kDual, "filter -F vpn_lockdown -w");

  datapath_->SetVpnLockdown(true);
  datapath_->SetVpnLockdown(false);
}

TEST_F(DatapathTest, SetConntrackHelpers) {
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kConntrackHelper, "1", ""));
  EXPECT_CALL(system_, SysNetSet(System::SysNet::kConntrackHelper, "0", ""));

  datapath_->SetConntrackHelpers(true);
  datapath_->SetConntrackHelpers(false);
}

TEST_F(DatapathTest, StartDnsRedirection_Default) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
      "DNAT --to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A redirect_default_dns -i vmtap0 -p tcp --dport 53 -j "
      "DNAT --to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -A redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
      "DNAT --to-destination ::1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
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

  datapath_->StartDnsRedirection(rule4);
  datapath_->StartDnsRedirection(rule6);
}

TEST_F(DatapathTest, StartDnsRedirection_User) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A redirect_user_dns -p udp --dport 53 -j DNAT "
      "--to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -A redirect_user_dns -p tcp --dport 53 -j DNAT "
      "--to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A accept_egress_to_dns_proxy -d 100.115.92.130 -j "
      "ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "nat -A snat_user_dns -p udp --dport 53 -j "
                             "MASQUERADE -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "nat -A snat_user_dns -p tcp --dport 53 -j "
                             "MASQUERADE -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -A redirect_user_dns -p udp --dport 53 -j DNAT "
      "--to-destination ::1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -A redirect_user_dns -p tcp --dport 53 -j DNAT "
      "--to-destination ::1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -A accept_egress_to_dns_proxy -d ::1 -j ACCEPT -w");

  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A skip_apply_vpn_mark -p udp --dport 53 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
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

  datapath_->StartDnsRedirection(rule4);
  datapath_->StartDnsRedirection(rule6);
}

TEST_F(DatapathTest, StartDnsRedirection_ExcludeDestination) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_user_dns -p udp ! -d 100.115.92.130 --dport "
      "53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -I redirect_user_dns -p tcp ! -d 100.115.92.130 --dport "
      "53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -A accept_egress_to_dns_proxy -d 100.115.92.130 -j "
      "ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -I redirect_user_dns -p udp ! -d ::1 --dport 53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -I redirect_user_dns -p tcp ! -d ::1 --dport 53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -A accept_egress_to_dns_proxy -d ::1 -j ACCEPT -w");

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

  datapath_->StartDnsRedirection(rule4);
  datapath_->StartDnsRedirection(rule6);
}

TEST_F(DatapathTest, StopDnsRedirection_Default) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
      "DNAT --to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_default_dns -i vmtap0 -p tcp --dport 53 -j "
      "DNAT --to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -D redirect_default_dns -i vmtap0 -p udp --dport 53 -j "
      "DNAT --to-destination ::1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
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

  datapath_->StopDnsRedirection(rule4);
  datapath_->StopDnsRedirection(rule6);
}

TEST_F(DatapathTest, StopDnsRedirection_User) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_user_dns -p udp --dport 53 -j DNAT "
      "--to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_user_dns -p tcp --dport 53 -j DNAT "
      "--to-destination 100.115.92.130 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D accept_egress_to_dns_proxy -d 100.115.92.130 -j "
      "ACCEPT -w");

  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "nat -D snat_user_dns -p udp --dport 53 -j "
                             "MASQUERADE -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "nat -D snat_user_dns -p tcp --dport 53 -j "
                             "MASQUERADE -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -D redirect_user_dns -p udp --dport 53 -j DNAT "
      "--to-destination ::1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -D redirect_user_dns -p tcp --dport 53 -j DNAT "
      "--to-destination ::1 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -D accept_egress_to_dns_proxy -d ::1 -j ACCEPT -w");

  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D skip_apply_vpn_mark -p udp --dport 53 -j ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kDual,
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

  datapath_->StopDnsRedirection(rule4);
  datapath_->StopDnsRedirection(rule6);
}

TEST_F(DatapathTest, StopDnsRedirection_ExcludeDestination) {
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_user_dns -p udp ! -d 100.115.92.130 --dport "
      "53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "nat -D redirect_user_dns -p tcp ! -d 100.115.92.130 --dport "
      "53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv4,
      "filter -D accept_egress_to_dns_proxy -d 100.115.92.130 -j "
      "ACCEPT -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -D redirect_user_dns -p udp ! -d ::1 --dport 53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "nat -D redirect_user_dns -p tcp ! -d ::1 --dport 53 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -D accept_egress_to_dns_proxy -d ::1 -j ACCEPT -w");

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

  datapath_->StopDnsRedirection(rule4);
  datapath_->StopDnsRedirection(rule6);
}

TEST_F(DatapathTest, PrefixEnforcement) {
  ShillClient::Device cell_device;
  cell_device.ifname = "wwan0";

  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -N egress_wwan0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "filter -I OUTPUT -o wwan0 -j egress_wwan0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6, "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_->StartSourceIPv6PrefixEnforcement(cell_device);

  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -A egress_wwan0 -s 2001:db8:1:1::/64 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6, "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_->UpdateSourceEnforcementIPv6Prefix(
      cell_device,
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1:1::/64"));

  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6, "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_->UpdateSourceEnforcementIPv6Prefix(cell_device, std::nullopt);

  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6,
      "filter -A egress_wwan0 -s 2001:db8:1:2::/64 -j RETURN -w");
  runner_.ExpectCallIptables(
      IpFamily::kIPv6, "filter -A egress_wwan0 -j enforce_ipv6_src_prefix -w");
  datapath_->UpdateSourceEnforcementIPv6Prefix(
      cell_device,
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1:2::/64"));

  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "filter -D OUTPUT -o wwan0 -j egress_wwan0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -F egress_wwan0 -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6, "filter -X egress_wwan0 -w");
  datapath_->StopSourceIPv6PrefixEnforcement(cell_device);
}

TEST_F(DatapathTest, SetRouteLocalnet) {
  EXPECT_CALL(system_,
              SysNetSet(System::SysNet::kIPv4RouteLocalnet, "1", "eth0"));
  EXPECT_CALL(system_,
              SysNetSet(System::SysNet::kIPv4RouteLocalnet, "0", "wlan0"));

  datapath_->SetRouteLocalnet("eth0", true);
  datapath_->SetRouteLocalnet("wlan0", false);
}

TEST_F(DatapathTest, ModprobeAll) {
  EXPECT_CALL(runner_, modprobe_all(ElementsAre("ip6table_filter", "ah6",
                                                "esp6", "nf_nat_ftp"),
                                    _));

  datapath_->ModprobeAll({"ip6table_filter", "ah6", "esp6", "nf_nat_ftp"});
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
  EXPECT_FALSE(datapath_->ModifyPortRule(request));
  Mock::VerifyAndClearExpectations(firewall_);

  // Invalid request #2
  request.set_op(patchpanel::ModifyPortRuleRequest::CREATE);
  request.set_proto(patchpanel::ModifyPortRuleRequest::INVALID_PROTOCOL);
  request.set_type(patchpanel::ModifyPortRuleRequest::ACCESS);
  EXPECT_CALL(*firewall_, AddAcceptRules(_, _, _)).Times(0);
  EXPECT_FALSE(datapath_->ModifyPortRule(request));
  Mock::VerifyAndClearExpectations(firewall_);

  // Invalid request #3
  request.set_op(patchpanel::ModifyPortRuleRequest::CREATE);
  request.set_proto(patchpanel::ModifyPortRuleRequest::TCP);
  request.set_type(patchpanel::ModifyPortRuleRequest::INVALID_RULE_TYPE);
  EXPECT_CALL(*firewall_, AddAcceptRules(_, _, _)).Times(0);
  EXPECT_FALSE(datapath_->ModifyPortRule(request));
  Mock::VerifyAndClearExpectations(firewall_);
}

TEST_F(DatapathTest, EnableQoSDetection) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -A qos_detect_static -j qos_detect -w");

  datapath_->EnableQoSDetection();
}

TEST_F(DatapathTest, DisableQoSDetection) {
  runner_.ExpectCallIptables(IpFamily::kDual,
                             "mangle -D qos_detect_static -j qos_detect -w");

  datapath_->DisableQoSDetection();
}

TEST_F(DatapathTest, EnableQoSApplyingDSCP) {
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -A POSTROUTING -o wlan0 -j qos_apply_dscp -w");

  datapath_->EnableQoSApplyingDSCP("wlan0");
}

TEST_F(DatapathTest, DisableQoSApplyingDSCP) {
  runner_.ExpectCallIptables(
      IpFamily::kDual, "mangle -D POSTROUTING -o wlan0 -j qos_apply_dscp -w");

  datapath_->DisableQoSApplyingDSCP("wlan0");
}

TEST_F(DatapathTest, UpdateDoHProvidersForQoSIPv4) {
  const std::vector<net_base::IPAddress> ipv4_input = {
      net_base::IPAddress::CreateFromString("1.2.3.4").value(),
      net_base::IPAddress::CreateFromString("5.6.7.8").value(),
  };

  runner_.ExpectCallIptables(IpFamily::kIPv4, "mangle -F qos_detect_doh -w");
  for (const auto& proto : {"tcp", "udp"}) {
    const std::string expected_rule =
        base::StrCat({"mangle -A qos_detect_doh -p ", proto,
                      " --dport 443 -d 1.2.3.4,5.6.7.8 -j MARK --set-xmark "
                      "0x00000060/0x000000e0 -w"});
    runner_.ExpectCallIptables(IpFamily::kIPv4, expected_rule);
  }

  datapath_->UpdateDoHProvidersForQoS(IpFamily::kIPv4, ipv4_input);
}

TEST_F(DatapathTest, UpdateDoHProvidersForQoSIPv6) {
  // Verify IPv6 input.
  const std::vector<net_base::IPAddress> ipv6_input = {
      net_base::IPAddress::CreateFromString("fd00::1").value(),
      net_base::IPAddress::CreateFromString("fd00::2").value(),
  };

  runner_.ExpectCallIptables(IpFamily::kIPv6, "mangle -F qos_detect_doh -w");
  for (const auto& proto : {"tcp", "udp"}) {
    const std::string expected_rule =
        base::StrCat({"mangle -A qos_detect_doh -p ", proto,
                      " --dport 443 -d fd00::1,fd00::2 -j MARK --set-xmark "
                      "0x00000060/0x000000e0 -w"});
    runner_.ExpectCallIptables(IpFamily::kIPv6, expected_rule);
  }

  datapath_->UpdateDoHProvidersForQoS(IpFamily::kIPv6, ipv6_input);
}

TEST_F(DatapathTest, UpdateDoHProvidersForQoSEmpty) {
  // Empty list should still trigger the flush, but no other rules.
  runner_.ExpectCallIptables(IpFamily::kIPv4, "mangle -F qos_detect_doh -w");
  datapath_->UpdateDoHProvidersForQoS(IpFamily::kIPv4, {});
}

TEST_F(DatapathTest, ModifyClatAcceptRules) {
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "filter -A FORWARD -i tun_nat64 -j ACCEPT -w");
  runner_.ExpectCallIptables(IpFamily::kIPv6,
                             "filter -A FORWARD -o tun_nat64 -j ACCEPT -w");
  datapath_->ModifyClatAcceptRules(Iptables::Command::kA, "tun_nat64");
}

TEST_F(DatapathTest, AddBorealisQosRule) {
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -A qos_detect_borealis "
      "-i vmtap6 -j MARK --set-xmark 0x00000020/0x000000e0 -w");
  datapath_->AddBorealisQoSRule("vmtap6");
}

TEST_F(DatapathTest, RemoveBorealisQosRule) {
  runner_.ExpectCallIptables(
      IpFamily::kDual,
      "mangle -D qos_detect_borealis "
      "-i vmtap6 -j MARK --set-xmark 0x00000020/0x000000e0 -w");
  datapath_->RemoveBorealisQoSRule("vmtap6");
}

}  // namespace patchpanel

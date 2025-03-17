// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <fcntl.h>
#include <linux/rtnetlink.h>
#include <sys/stat.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/rtnl_message.h>
#include <chromeos/patchpanel/address_manager.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <chromeos/patchpanel/mock_message_dispatcher.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxy-mocks.h>
#include <shill/dbus/client/fake_client.h>

#include "dns-proxy/ipc.pb.h"

namespace dns_proxy {
namespace {

constexpr net_base::IPv4Address kNetnsPeerIPv4Aaddr(100, 115, 92, 130);
constexpr net_base::IPv6Address kNetnsPeerIPv6Aaddr(
    0xfd, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01);
constexpr base::TimeDelta kRequestTimeout = base::Seconds(10000);
constexpr base::TimeDelta kRequestRetryDelay = base::Milliseconds(200);
constexpr int32_t kRequestMaxRetry = 1;

int make_fd() {
  return open("/dev/null", O_RDONLY);
}

// A helper function to convert a list of IP addresses from type std::string to
// net_base::IPAddress. If |list2| is provided, its elements will be appended to
// the results. This function assumes that all input strings are valid IP
// addresses, otherwise it will crash directly.
std::vector<net_base::IPAddress> StringsToIPAddressesChecked(
    const std::vector<std::string>& list1,
    const std::vector<std::string>& list2 = {}) {
  std::vector<net_base::IPAddress> ret;
  for (const auto& str : list1) {
    ret.push_back(*net_base::IPAddress::CreateFromString(str));
  }
  for (const auto& str : list2) {
    ret.push_back(*net_base::IPAddress::CreateFromString(str));
  }
  return ret;
}

}  // namespace

using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ManagerProxyMock;
using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

patchpanel::Client::VirtualDevice virtualdev(
    patchpanel::Client::GuestType guest_type,
    const std::string& ifname,
    const std::string& phys_ifname,
    const net_base::IPv4Address& host_ipv4_addr = {}) {
  patchpanel::Client::VirtualDevice device;
  device.ifname = ifname;
  device.phys_ifname = phys_ifname;
  device.guest_type = guest_type;
  device.host_ipv4_addr = host_ipv4_addr;
  return device;
}

class FakeShillClient : public shill::FakeClient {
 public:
  FakeShillClient(scoped_refptr<dbus::Bus> bus,
                  ManagerProxyInterface* manager_proxy)
      : shill::FakeClient(bus), manager_proxy_(manager_proxy) {}

  std::unique_ptr<shill::Client::ManagerPropertyAccessor> ManagerProperties(
      const base::TimeDelta& timeout) const override {
    return std::make_unique<shill::Client::ManagerPropertyAccessor>(
        manager_proxy_);
  }

  std::unique_ptr<shill::Client::Device> DefaultDevice(
      bool exclude_vpn) override {
    return std::move(default_device_);
  }

  ManagerProxyInterface* GetManagerProxy() const override {
    return manager_proxy_;
  }

  std::unique_ptr<shill::Client::Device> default_device_;

 private:
  ManagerProxyInterface* manager_proxy_;
};

class MockPatchpanelClient : public patchpanel::FakeClient {
 public:
  MockPatchpanelClient() = default;
  ~MockPatchpanelClient() override = default;

  MOCK_METHOD(
      (std::pair<base::ScopedFD, patchpanel::Client::ConnectedNamespace>),
      ConnectNamespace,
      (pid_t pid,
       const std::string& outbound_ifname,
       bool forward_user_traffic,
       bool route_on_vpn,
       patchpanel::Client::TrafficSource traffic_source,
       bool static_ipv6),
      (override));
  MOCK_METHOD(base::ScopedFD,
              RedirectDns,
              (patchpanel::Client::DnsRedirectionRequestType,
               const std::string&,
               const std::string&,
               const std::vector<std::string>&,
               const std::string&),
              (override));
  MOCK_METHOD(std::vector<patchpanel::Client::VirtualDevice>,
              GetDevices,
              (),
              (override));
};

class MockResolver : public Resolver {
 public:
  MockResolver()
      : Resolver(base::DoNothing(),
                 kRequestTimeout,
                 kRequestRetryDelay,
                 kRequestMaxRetry) {}
  ~MockResolver() override = default;

  MOCK_METHOD(bool,
              ListenUDP,
              (struct sockaddr*, std::string_view),
              (override));
  MOCK_METHOD(bool,
              ListenTCP,
              (struct sockaddr*, std::string_view),
              (override));
  MOCK_METHOD(void, StopListen, (sa_family_t, std::string_view), (override));
  MOCK_METHOD(void,
              SetNameServers,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void,
              SetDoHProviders,
              (const std::vector<std::string>&, bool),
              (override));
  MOCK_METHOD(void, SetInterface, (std::string_view), (override));
  MOCK_METHOD(void, ClearInterface, (), (override));
};

class TestProxy : public Proxy {
 public:
  TestProxy(const Options& opts,
            std::unique_ptr<patchpanel::Client> patchpanel,
            std::unique_ptr<shill::Client> shill,
            std::unique_ptr<patchpanel::MessageDispatcher<SubprocessMessage>>
                msg_dispatcher,
            bool root_ns_enabled_)
      : Proxy(opts,
              std::move(patchpanel),
              std::move(shill),
              std::move(msg_dispatcher),
              root_ns_enabled_) {}

  std::unique_ptr<Resolver> resolver;
  std::unique_ptr<Resolver> NewResolver(base::TimeDelta timeout,
                                        base::TimeDelta retry_delay,
                                        int max_num_retries) override {
    return std::move(resolver);
  }

  std::map<std::string, int> ifindexes;
  // Returns a consistent mapping between |ifname| and its index value.
  int IfNameToIndex(const char* ifname) override {
    static int cur_index = 1;
    const std::string ifname_str(ifname);
    const auto it = ifindexes.find(ifname_str);
    if (it != ifindexes.end()) {
      return it->second;
    }
    ifindexes[ifname_str] = cur_index;
    return cur_index++;
  }
};

class ProxyTest : public ::testing::Test,
                  public ::testing::WithParamInterface<bool> {
 protected:
  ProxyTest()
      : mock_bus_(new dbus::MockBus{dbus::Bus::Options{}}),
        mock_proxy_(new dbus::MockObjectProxy(mock_bus_.get(),
                                              shill::kFlimflamServiceName,
                                              dbus::ObjectPath("/"))) {}
  ~ProxyTest() override { mock_bus_->ShutdownAndBlock(); }

  void SetUp() override {
    EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
        .WillRepeatedly(Return(mock_proxy_.get()));
  }

  void SetUpProxy(bool root_ns_enabled,
                  const Proxy::Options& opts,
                  std::unique_ptr<shill::Client::Device> device = nullptr,
                  bool set_resolver = true) {
    // Set up mocks and fakes.
    patchpanel_client_ = new MockPatchpanelClient();
    shill_client_ = new FakeShillClient(
        mock_bus_, reinterpret_cast<ManagerProxyInterface*>(
                       const_cast<ManagerProxyMock*>(&mock_manager_)));
    msg_dispatcher_ =
        new patchpanel::MockMessageDispatcher<SubprocessMessage>();

    // Initialize Proxy instance.
    proxy_ = std::make_unique<TestProxy>(
        opts, base::WrapUnique(patchpanel_client_),
        base::WrapUnique(shill_client_), base::WrapUnique(msg_dispatcher_),
        root_ns_enabled);

    // Initialize default proxy behaviors.
    proxy_->shill_ready_ = true;
    proxy_->device_ = std::move(device);
    if (set_resolver) {
      resolver_ = new MockResolver();
      proxy_->resolver_ = base::WrapUnique(resolver_);
      proxy_->doh_config_.set_resolver(resolver_);
    }

    // Initialize default mocks behavior.
    if (opts.type == Proxy::Type::kSystem) {
      ON_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
          .WillByDefault(Return(true));
      ON_CALL(*msg_dispatcher_, SendMessage(_)).WillByDefault(Return(true));
    }

    // Initialize expected addresses.
    if (root_ns_enabled) {
      switch (opts.type) {
        case Proxy::Type::kSystem:
          ipv4_address_ = patchpanel::kDnsProxySystemIPv4Address;
          ipv6_address_ = patchpanel::kDnsProxySystemIPv6Address;
          break;
        case Proxy::Type::kDefault:
          ipv4_address_ = patchpanel::kDnsProxyDefaultIPv4Address;
          ipv6_address_ = patchpanel::kDnsProxyDefaultIPv6Address;
          break;
        default:
          break;
      }
    } else {
      ipv4_address_ = kNetnsPeerIPv4Aaddr;
      ipv6_address_ = kNetnsPeerIPv6Aaddr;
    }
  }

  std::unique_ptr<shill::Client::Device> ShillDevice(
      shill::Client::Device::ConnectionState state =
          shill::Client::Device::ConnectionState::kOnline,
      shill::Client::Device::Type type = shill::Client::Device::Type::kEthernet,
      const std::string& ifname = "",
      std::vector<std::string> ipv4_nameservers = {"8.8.8.8"},
      std::vector<std::string> ipv6_nameservers = {"2001:4860:4860::8888"}) {
    auto dev = std::make_unique<shill::Client::Device>();
    dev->type = type;
    dev->state = state;
    dev->ifname = ifname;
    dev->network_config.dns_servers =
        StringsToIPAddressesChecked(ipv6_nameservers, ipv4_nameservers);
    return dev;
  }

  void SetListenAddresses(
      const std::optional<net_base::IPv4Address>& ipv4_addr,
      const std::optional<net_base::IPv6Address>& ipv6_addr) {
    proxy_->initialized_ = true;
    proxy_->ipv4_address_ = ipv4_addr;
    proxy_->ipv6_address_ = ipv6_addr;
    if (proxy_->root_ns_enabled_) {
      return;
    }
    proxy_->ns_fd_ = base::ScopedFD(make_fd());
    if (ipv4_addr) {
      proxy_->ns_.peer_ipv4_address = *ipv4_addr;
    }
  }

  void SetNameServers(const std::vector<std::string>& ipv4_nameservers,
                      const std::vector<std::string>& ipv6_nameservers) {
    EXPECT_TRUE(proxy_->device_);
    proxy_->device_->network_config.dns_servers =
        StringsToIPAddressesChecked(ipv6_nameservers, ipv4_nameservers);
    proxy_->UpdateNameServers();
  }

  void SetInterfaceIPv6Address(const std::string& ifname,
                               const net_base::IPv6Address& addr) {
    int ifindex = proxy_->IfNameToIndex(ifname.c_str());
    proxy_->link_local_addresses_[ifindex] = addr;
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  ManagerProxyMock mock_manager_;

  MockResolver* resolver_;
  patchpanel::MockMessageDispatcher<SubprocessMessage>* msg_dispatcher_;
  FakeShillClient* shill_client_;
  MockPatchpanelClient* patchpanel_client_;
  std::unique_ptr<TestProxy> proxy_;

  net_base::IPv4Address ipv4_address_;
  net_base::IPv6Address ipv6_address_;
};

// Test with DNS proxy running on root namespace and inside a network namespace.
INSTANTIATE_TEST_SUITE_P(ProxyTestInstance,
                         ProxyTest,
                         ::testing::Values(false, true));

TEST_P(ProxyTest, NonSystemProxy_OnShutdownDoesNotCallShill) {
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _)).Times(0);
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _)).Times(0);
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice());
  int unused;
  proxy_->OnShutdown(&unused);
}

TEST_P(ProxyTest, SystemProxy_SendIPAddressesToController) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});

  ProxyMessage proxy_msg;
  proxy_msg.set_type(ProxyMessage::SET_ADDRS);
  proxy_msg.add_addrs(ipv4_address_.ToString());
  proxy_msg.add_addrs(ipv6_address_.ToString());
  SubprocessMessage msg;
  *msg.mutable_proxy_message() = proxy_msg;
  EXPECT_CALL(*msg_dispatcher_, SendMessage(EqualsProto(msg)))
      .WillOnce(Return(true));
  proxy_->SendIPAddressesToController(ipv4_address_, ipv6_address_);
}

TEST_P(ProxyTest, SystemProxy_SendIPAddressesToControllerEmptyNameserver) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());

  // Only IPv4 nameserver.
  SetNameServers({"8.8.8.8"}, /*ipv6_nameservers=*/{});
  ProxyMessage proxy_msg;
  proxy_msg.set_type(ProxyMessage::SET_ADDRS);
  proxy_msg.add_addrs(ipv4_address_.ToString());
  SubprocessMessage msg;
  *msg.mutable_proxy_message() = proxy_msg;
  EXPECT_CALL(*msg_dispatcher_, SendMessage(EqualsProto(msg)))
      .WillOnce(Return(true));
  proxy_->SendIPAddressesToController(ipv4_address_, ipv6_address_);

  // Only IPv6 nameserver.
  SetNameServers(/*ipv4_nameservers=*/{}, {"2001:4860:4860::8888"});
  proxy_msg.Clear();
  proxy_msg.set_type(ProxyMessage::SET_ADDRS);
  proxy_msg.add_addrs(ipv6_address_.ToString());
  *msg.mutable_proxy_message() = proxy_msg;
  EXPECT_CALL(*msg_dispatcher_, SendMessage(EqualsProto(msg)))
      .WillOnce(Return(true));
  proxy_->SendIPAddressesToController(ipv4_address_, ipv6_address_);
}

TEST_P(ProxyTest, SystemProxy_ClearIPAddressesInController) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  EXPECT_CALL(*msg_dispatcher_, SendMessage(_)).WillOnce(Return(true));
  proxy_->ClearIPAddressesInController();
}

TEST_P(ProxyTest, ShillInitializedWhenReady) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});

  // Test class defaults to make shill client ready. Reset to false.
  proxy_->shill_ready_ = false;
  proxy_->OnShillReady(true);
  EXPECT_TRUE(proxy_->shill_ready_);
}

TEST_P(ProxyTest, SystemProxy_ConnectedNamedspace) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*patchpanel_client_, ConnectNamespace(_, _, _, _, _, _))
        .Times(0);
  } else {
    EXPECT_CALL(
        *patchpanel_client_,
        ConnectNamespace(_, _, /*outbound_ifname=*/"", /*route_on_vpn=*/false,
                         patchpanel::Client::TrafficSource::kSystem, _))
        .WillOnce(
            Return(std::make_pair(base::ScopedFD(make_fd()),
                                  patchpanel::Client::ConnectedNamespace{})));
  }
  proxy_->OnPatchpanelReady(true);
}

TEST_P(ProxyTest, DefaultProxy_ConnectedNamedspace) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice());

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*patchpanel_client_, ConnectNamespace(_, _, _, _, _, _))
        .Times(0);
  } else {
    EXPECT_CALL(
        *patchpanel_client_,
        ConnectNamespace(_, _, /*outbound_ifname=*/"", /*route_on_vpn=*/true,
                         patchpanel::Client::TrafficSource::kUser, _))
        .WillOnce(
            Return(std::make_pair(base::ScopedFD(make_fd()),
                                  patchpanel::Client::ConnectedNamespace{})));
  }
  proxy_->OnPatchpanelReady(true);
}

TEST_P(ProxyTest, ArcProxy_ConnectedNamedspace) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"});

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*patchpanel_client_, ConnectNamespace(_, _, _, _, _, _))
        .Times(0);
  } else {
    EXPECT_CALL(*patchpanel_client_,
                ConnectNamespace(_, _, /*outbound_ifname=*/"eth0",
                                 /*route_on_vpn=*/false,
                                 patchpanel::Client::TrafficSource::kArc, _))
        .WillOnce(
            Return(std::make_pair(base::ScopedFD(make_fd()),
                                  patchpanel::Client::ConnectedNamespace{})));
  }
  proxy_->OnPatchpanelReady(true);
}

TEST_P(ProxyTest, StateClearedIfDefaultServiceDrops) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());

  proxy_->OnDefaultDeviceChanged(nullptr /* no service */);
  EXPECT_FALSE(proxy_->device_);
  EXPECT_FALSE(proxy_->resolver_);
}

TEST_P(ProxyTest, ArcProxy_IgnoredIfDefaultServiceDrops) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());

  proxy_->OnDefaultDeviceChanged(nullptr /* no service */);
  EXPECT_TRUE(proxy_->device_);
  EXPECT_TRUE(proxy_->resolver_);
}

TEST_P(ProxyTest, StateClearedIfDefaultServiceIsNotOnline) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kReady);
  proxy_->OnDefaultDeviceChanged(dev.get());

  EXPECT_FALSE(proxy_->device_);
  EXPECT_FALSE(proxy_->resolver_);
}

TEST_P(ProxyTest, NewResolverStartsListeningOnDefaultServiceComesOnline) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             /*device=*/nullptr, /*set_resolver=*/false);
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto* new_resolver = new MockResolver();
  proxy_->resolver = base::WrapUnique(new_resolver);
  if (proxy_->root_ns_enabled_) {
    // Called for both IPv4 and IPv6.
    EXPECT_CALL(*new_resolver, ListenUDP(_, _))
        .Times(2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*new_resolver, ListenTCP(_, _))
        .Times(2)
        .WillRepeatedly(Return(true));
  } else {
    // Called for IPv6 only.
    EXPECT_CALL(*new_resolver, ListenUDP(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*new_resolver, ListenTCP(_, _)).WillOnce(Return(true));
  }

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline);
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  proxy_->OnDefaultDeviceChanged(dev.get());
  EXPECT_TRUE(proxy_->resolver_);
}

TEST_P(ProxyTest, NameServersUpdatedOnDefaultServiceComesOnline) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8", "8.8.4.4"},
                         {"2001:4860:4860::8888", "2001:4860:4860::8844"});
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy_->OnDefaultDeviceChanged(dev.get());
}

TEST_P(ProxyTest, SystemProxy_IgnoresVPN) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // Expect default device changes to WiFi.
  auto wifi = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                          shill::Client::Device::Type::kWifi);
  proxy_->OnDefaultDeviceChanged(wifi.get());
  EXPECT_EQ(proxy_->device_->type, shill::Client::Device::Type::kWifi);

  // Expect default device to still be WiFi even when a VPN is active.
  auto vpn = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kVPN);
  proxy_->OnDefaultDeviceChanged(vpn.get());
  EXPECT_EQ(proxy_->device_->type, shill::Client::Device::Type::kWifi);
}

TEST_P(ProxyTest, SystemProxy_GetsPhysicalDeviceOnInitialVPN) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  shill_client_->default_device_ =
      ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                  shill::Client::Device::Type::kWifi);

  auto vpn = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kVPN);
  proxy_->OnDefaultDeviceChanged(vpn.get());
  EXPECT_TRUE(proxy_->device_);
  EXPECT_EQ(proxy_->device_->type, shill::Client::Device::Type::kWifi);
}

TEST_P(ProxyTest, DefaultProxy_UsesVPN) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto wifi = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                          shill::Client::Device::Type::kWifi);
  proxy_->OnDefaultDeviceChanged(wifi.get());
  EXPECT_TRUE(proxy_->device_);
  EXPECT_EQ(proxy_->device_->type, shill::Client::Device::Type::kWifi);

  auto vpn = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kVPN);
  proxy_->OnDefaultDeviceChanged(vpn.get());
  EXPECT_TRUE(proxy_->device_);
  EXPECT_EQ(proxy_->device_->type, shill::Client::Device::Type::kVPN);
}

TEST_P(ProxyTest, ArcProxy_NameServersUpdatedOnDeviceChangeEvent) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // Set name servers on device change event.
  auto wifi = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                          shill::Client::Device::Type::kWifi, "wlan0",
                          {"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"),
                                         StrEq("2001:4860:4860::8888"))));
  proxy_->OnDeviceChanged(wifi.get());

  // Verify it only applies changes for the correct interface.
  auto eth = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8", "8.8.4.4"},
                         {"2001:4860:4860::8888", "2001:4860:4860::8844"});
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(0);
  proxy_->OnDeviceChanged(eth.get());

  // Update WiFi device nameservers.
  wifi->network_config.dns_servers = StringsToIPAddressesChecked(
      {"2001:4860:4860::8888", "2001:4860:4860::8844", "8.8.8.8", "8.8.4.4"});
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy_->OnDeviceChanged(wifi.get());
}

TEST_P(ProxyTest, SystemProxy_NameServersUpdatedOnDeviceChangeEvent) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // Set name servers on device change event.
  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"),
                                         StrEq("2001:4860:4860::8888"))));
  proxy_->OnDefaultDeviceChanged(dev.get());

  // Now trigger an NetworkConfig change.
  dev->network_config.dns_servers = StringsToIPAddressesChecked(
      {"2001:4860:4860::8888", "2001:4860:4860::8844", "8.8.8.8", "8.8.4.4"});
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy_->OnDeviceChanged(dev.get());
}

TEST_P(ProxyTest, DeviceChangeEventIgnored) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0");

  // Set name servers on device change event.
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(1);
  proxy_->OnDefaultDeviceChanged(dev.get());

  // No change to NetworkConfig, no call to SetNameServers
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(0);
  proxy_->OnDeviceChanged(dev.get());

  // Different ifname, no call to SetNameServers
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(0);
  dev->ifname = "eth1";
  proxy_->OnDeviceChanged(dev.get());
}

TEST_P(ProxyTest, BasicDoHDisable) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  brillo::VariantDictionary props;
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, BasicDoHAlwaysOn) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), true));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, BasicDoHAutomatic) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"8.8.4.4"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, BasicDoHSecureWithFallback) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"8.8.4.4"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(*resolver_,
              SetDoHProviders(ElementsAre(StrEq("https://custom-provider.com")),
                              false));
  brillo::VariantDictionary props;
  props["https://custom-provider.com"] =
      std::string(shill::kDNSProxyDOHProvidersMatchAnyIPAddress);
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, RemovesDNSQueryParameterTemplate_AlwaysOn) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), true));
  brillo::VariantDictionary props;
  props["https://dns.google.com{?dns}"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, RemovesDNSQueryParameterTemplate_Automatic) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"8.8.4.4"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com{?dns}"] = std::string("8.8.8.8, 8.8.4.4");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, RemovesDNSQueryParameterTemplate_SecureWithFallback) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"8.8.4.4"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(*resolver_,
              SetDoHProviders(ElementsAre(StrEq("https://custom-provider.com")),
                              false));
  brillo::VariantDictionary props;
  props["https://custom-provider.com{?dns}"] =
      std::string(shill::kDNSProxyDOHProvidersMatchAnyIPAddress);
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, NewResolverConfiguredWhenSet) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  props["https://chrome.cloudflare-dns.com/dns-query"] =
      std::string("1.1.1.1,2606:4700:4700::1111");
  proxy_->OnDoHProvidersChanged(props);

  SetNameServers({"1.0.0.1", "1.1.1.1"}, /*ipv6_nameservers=*/{});
  EXPECT_CALL(*resolver_, SetNameServers(UnorderedElementsAre(
                              StrEq("1.1.1.1"), StrEq("1.0.0.1"))));
  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(
          ElementsAre(StrEq("https://chrome.cloudflare-dns.com/dns-query")),
          false));
  proxy_->doh_config_.set_resolver(resolver_);
}

TEST_P(ProxyTest, DoHModeChangingFixedNameServers) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  // Initially off.
  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  SetNameServers({"1.1.1.1", "9.9.9.9"}, /*ipv6_nameservers=*/{});

  // Automatic mode - matched cloudflare.
  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(
          ElementsAre(StrEq("https://chrome.cloudflare-dns.com/dns-query")),
          false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  props["https://chrome.cloudflare-dns.com/dns-query"] =
      std::string("1.1.1.1,2606:4700:4700::1111");
  proxy_->OnDoHProvidersChanged(props);

  // Automatic mode - no match.
  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  SetNameServers({"10.10.10.1"}, /*ipv6_nameservers=*/{});

  // Automatic mode - matched google.
  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  SetNameServers({"8.8.4.4", "10.10.10.1", "8.8.8.8"}, /*ipv6_nameservers=*/{});

  // Automatic mode - secure DNS with fallback.
  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(
          ElementsAre(StrEq("https://custom-provider.com/dns-query")), false));
  props["https://custom-provider.com/dns-query"] =
      std::string(shill::kDNSProxyDOHProvidersMatchAnyIPAddress);
  proxy_->OnDoHProvidersChanged(props);

  // Explicitly turned off.
  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  props.clear();
  proxy_->OnDoHProvidersChanged(props);

  // Still off - even switching ns back.
  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  SetNameServers({"8.8.4.4", "10.10.10.1", "8.8.8.8"}, /*ipv6_nameservers=*/{});

  // Always-on mode.
  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://doh.opendns.com/dns-query")),
                      true));
  props.clear();
  props["https://doh.opendns.com/dns-query"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);

  // Back to automatic mode, though no matching ns.
  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  props.clear();
  props["https://doh.opendns.com/dns-query"] = std::string(
      "208.67.222.222,208.67.220.220,2620:119:35::35, 2620:119:53::53");
  proxy_->OnDoHProvidersChanged(props);

  // Automatic mode working on ns update.
  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://doh.opendns.com/dns-query")),
                      false));
  SetNameServers({"8.8.8.8"}, {"2620:119:35::35"});
}

TEST_P(ProxyTest, MultipleDoHProvidersForAlwaysOnMode) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(*resolver_, SetDoHProviders(UnorderedElementsAre(
                                              StrEq("https://dns.google.com"),
                                              StrEq("https://doh.opendns.com")),
                                          true));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  props["https://doh.opendns.com"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, MultipleDoHProvidersForAutomaticMode) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  SetNameServers({"1.1.1.1", "10.10.10.10"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(
          ElementsAre(StrEq("https://chrome.cloudflare-dns.com/dns-query")),
          false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  props["https://dns.quad9.net/dns-query"] = std::string("9.9.9.9,2620:fe::9");
  props["https://chrome.cloudflare-dns.com/dns-query"] =
      std::string("1.1.1.1,2606:4700:4700::1111");
  props["https://doh.opendns.com/dns-query"] = std::string(
      "208.67.222.222,208.67.220.220,2620:119:35::35, 2620:119:53::53");
  proxy_->OnDoHProvidersChanged(props);

  EXPECT_CALL(*resolver_,
              SetDoHProviders(UnorderedElementsAre(
                                  StrEq("https://dns.google.com"),
                                  StrEq("https://doh.opendns.com/dns-query"),
                                  StrEq("https://dns.quad9.net/dns-query")),
                              false));
  SetNameServers({"8.8.8.8", "10.10.10.10"}, {"2620:fe::9", "2620:119:53::53"});
}

TEST_P(ProxyTest, MultipleDoHProvidersForSecureWithFallbackMode) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  SetNameServers({"1.1.1.1", "10.10.10.10"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(UnorderedElementsAre(
                          StrEq("https://custom-provider-1.com"),
                          StrEq("https://custom-provider-2.com/dns-query")),
                      false));
  brillo::VariantDictionary props;
  props["https://custom-provider-1.com"] =
      std::string(shill::kDNSProxyDOHProvidersMatchAnyIPAddress);
  props["https://custom-provider-2.com/dns-query"] =
      std::string(shill::kDNSProxyDOHProvidersMatchAnyIPAddress);
  proxy_->OnDoHProvidersChanged(props);
}

TEST_P(ProxyTest, DoHBadAlwaysOnConfigSetsAutomaticMode) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"1.1.1.1", "10.10.10.10"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(
          ElementsAre(StrEq("https://chrome.cloudflare-dns.com/dns-query")),
          false));
  brillo::VariantDictionary props;
  props["https://dns.opendns.com"] = std::string("");
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  props["https://dns.quad9.net/dns-query"] = std::string("9.9.9.9,2620:fe::9");
  props["https://chrome.cloudflare-dns.com/dns-query"] =
      std::string("1.1.1.1,2606:4700:4700::1111");
  props["https://doh.opendns.com/dns-query"] = std::string(
      "208.67.222.222,208.67.220.220,2620:119:35::35, 2620:119:53::53");
  proxy_->OnDoHProvidersChanged(props);

  EXPECT_CALL(*resolver_,
              SetDoHProviders(UnorderedElementsAre(
                                  StrEq("https://dns.google.com"),
                                  StrEq("https://doh.opendns.com/dns-query"),
                                  StrEq("https://dns.quad9.net/dns-query")),
                              false));
  SetNameServers({"8.8.8.8", "10.10.10.10"}, {"2620:fe::9", "2620:119:53::53"});
}

TEST_P(ProxyTest, SystemProxy_SetsDnsRedirectionRule) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // System proxy requests a DnsRedirectionRule to exclude traffic destined
  // not to the underlying network's name server.
  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0");
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(
          patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination, _,
          ipv4_address_.ToString(), _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(
          patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination, _,
          ipv6_address_.ToString(), _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnDefaultDeviceChanged(dev.get());

  // System proxy does not call patchpanel on Parallels VM started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap1",
                 "eth0"));

  // System proxy does not call patchpanel on ARC started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kArcContainer, "arc_eth0",
                 "eth0"));
}

TEST_P(ProxyTest, SystemProxy_NeverListenForGuests) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  // System proxy does not listen for guests
  EXPECT_CALL(*new_resolver, ListenUDP(_, _)).Times(0);
  EXPECT_CALL(*new_resolver, ListenTCP(_, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap1",
                 "eth0"));
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kArcContainer, "arc_eth0",
                 "eth0"));
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleDeviceAlreadyStarted) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // Set DNS redirection rule.
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("8.8.8.8"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("2001:4860:4860::8888"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->ApplyDeviceUpdate();
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleNewDeviceStarted) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // Empty active device.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->ApplyDeviceUpdate();
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);

  // Default device changed.
  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("8.8.8.8"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("2001:4860:4860::8888"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnDefaultDeviceChanged(dev.get());
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleGuest) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  SetInterfaceIPv6Address("vmtap0",
                          *net_base::IPv6Address::CreateFromString("fd00::1"));

  // Guest started.
  auto plugin_vm_dev =
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap0", "eth0",
                 net_base::IPv4Address(192, 168, 1, 1));
  net_base::IPv4Address addr4 =
      proxy_->root_ns_enabled_ ? plugin_vm_dev.host_ipv4_addr : ipv4_address_;
  net_base::IPv6Address addr6 =
      proxy_->root_ns_enabled_
          ? *net_base::IPv6Address::CreateFromString("fd00::1")
          : ipv6_address_;
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", addr4.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", addr6.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);

  // Guest stopped.
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_P(ProxyTest, DefaultProxy_ListenForGuests) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  // Guest started.
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, ListenUDP(_, "vmtap0")).WillOnce(Return(true));
    EXPECT_CALL(*new_resolver, ListenTCP(_, "vmtap0")).WillOnce(Return(true));
  } else {
    EXPECT_CALL(*new_resolver, ListenUDP(_, _)).Times(0);
    EXPECT_CALL(*new_resolver, ListenTCP(_, _)).Times(0);
  }
  auto plugin_vm_dev =
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap0", "eth0");
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 plugin_vm_dev);

  // Guest stopped.
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, StopListen(_, "vmtap0")).Times(2);
  } else {
    EXPECT_CALL(*new_resolver, StopListen(_, _)).Times(0);
  }
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, plugin_vm_dev);
}

TEST_P(ProxyTest, DefaultProxy_NeverSetsDnsRedirectionRuleOtherGuest) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // Other guest started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kArcContainer, "arc_eth0",
                 "eth0"));
}

TEST_P(ProxyTest, DefaultProxy_NeverListenForOtherGuests) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  // Other guest started.
  EXPECT_CALL(*new_resolver, ListenUDP(_, _)).Times(0);
  EXPECT_CALL(*new_resolver, ListenTCP(_, _)).Times(0);
  auto arc_dev = virtualdev(patchpanel::Client::GuestType::kArcContainer,
                            "arc_eth0", "eth0");
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 arc_dev);

  // Other guest stopped.
  EXPECT_CALL(*new_resolver, StopListen(_, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, arc_dev);
}

TEST_P(ProxyTest, SystemProxy_SetDnsRedirectionRuleIPv6Added) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetListenAddresses(ipv4_address_, /*ipv6_addr=*/std::nullopt);

  // Test only applicable for specific network namespace.
  if (proxy_->root_ns_enabled_) {
    return;
  }

  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(
          patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination, _,
          ipv6_address_.ToString(), _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  int ifindex = proxy_->IfNameToIndex(proxy_->ns_.peer_ifname.c_str());
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeAdd, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, ifindex, AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg);
}

TEST_P(ProxyTest, SystemProxy_SetDnsRedirectionRuleIPv6Deleted) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());

  // Test only applicable for specific network namespace.
  if (proxy_->root_ns_enabled_) {
    return;
  }

  proxy_->lifeline_fds_.emplace(std::make_pair("", AF_INET6),
                                base::ScopedFD(make_fd()));

  int ifindex = proxy_->IfNameToIndex(proxy_->ns_.peer_ifname.c_str());
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeDelete, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, ifindex, AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  proxy_->RTNLMessageHandler(msg);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleWithoutIPv6) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});
  SetListenAddresses(ipv4_address_, /*ipv6_addr=*/std::nullopt);

  // Default device changed.
  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("8.8.8.8"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnDefaultDeviceChanged(dev.get());
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 1);

  // Guest started.
  auto plugin_vm_dev =
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap0", "eth0",
                 net_base::IPv4Address(192, 168, 1, 1));
  net_base::IPv4Address addr =
      proxy_->root_ns_enabled_ ? plugin_vm_dev.host_ipv4_addr : ipv4_address_;
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);

  // Guest stopped.
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 1);
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Added) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetListenAddresses(ipv4_address_, /*ipv6_addr=*/std::nullopt);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  if (!proxy_->root_ns_enabled_) {
    EXPECT_CALL(
        *patchpanel_client_,
        RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser, _,
                    ipv6_address_.ToString(), _, _))
        .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  }

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0")}));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", ipv6_address_.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, ListenUDP(_, "vmtap0")).WillOnce(Return(true));
    EXPECT_CALL(*new_resolver, ListenTCP(_, "vmtap0")).WillOnce(Return(true));
  }

  std::string ifname =
      proxy_->root_ns_enabled_ ? "vmtap0" : proxy_->ns_.peer_ifname;
  int ifindex = proxy_->IfNameToIndex(ifname.c_str());
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeAdd, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, ifindex, AF_INET6);
  int scope = proxy_->root_ns_enabled_ ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
  msg.set_address_status(net_base::RTNLMessage::AddressStatus(0, 0, scope));
  msg.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg);
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Deleted) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice());
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  proxy_->lifeline_fds_.emplace(std::make_pair("", AF_INET6),
                                base::ScopedFD(make_fd()));
  proxy_->lifeline_fds_.emplace(std::make_pair("vmtap0", AF_INET6),
                                base::ScopedFD(make_fd()));

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0")}));

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, StopListen(AF_INET6, "vmtap0")).Times(1);
  }

  std::string ifname =
      proxy_->root_ns_enabled_ ? "vmtap0" : proxy_->ns_.peer_ifname;
  int ifindex = proxy_->IfNameToIndex(ifname.c_str());
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeDelete, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, ifindex, AF_INET6);
  int scope = proxy_->root_ns_enabled_ ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
  msg.set_address_status(net_base::RTNLMessage::AddressStatus(0, 0, scope));
  proxy_->RTNLMessageHandler(msg);
  if (proxy_->root_ns_enabled_) {
    EXPECT_EQ(proxy_->lifeline_fds_.size(), 1);
  } else {
    EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
  }
}

TEST_P(ProxyTest, DefaultProxy_SetDnsRedirectionRuleUnrelatedIPv6Added) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice());

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillRepeatedly(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0")}));
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);

  net_base::RTNLMessage msg_unrelated_ifindex(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_ifindex.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg_unrelated_ifindex.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_ifindex);

  net_base::RTNLMessage msg_unrelated_scope(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_scope.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_LINK));
  msg_unrelated_scope.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_scope);
}

TEST_P(ProxyTest, ArcProxy_SetDnsRedirectionRuleDeviceAlreadyStarted) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  SetListenAddresses(ipv4_address_, ipv6_address_);
  SetInterfaceIPv6Address("arc_eth0",
                          *net_base::IPv6Address::CreateFromString("fd00::1"));

  net_base::IPv4Address addr4 = proxy_->root_ns_enabled_
                                    ? net_base::IPv4Address(192, 168, 1, 1)
                                    : ipv4_address_;
  net_base::IPv6Address addr6 =
      proxy_->root_ns_enabled_
          ? *net_base::IPv6Address::CreateFromString("fd00::1")
          : ipv6_address_;

  // Set devices created before the proxy started.
  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::Client::VirtualDevice>{
          virtualdev(patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0",
                     net_base::IPv4Address(192, 168, 1, 1))}));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                          "arc_eth0", addr4.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                          "arc_eth0", addr6.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->ApplyDeviceUpdate();
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);
}

TEST_P(ProxyTest, ArcProxy_SetDnsRedirectionRuleNewDeviceStarted) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  SetListenAddresses(ipv4_address_, ipv6_address_);
  SetInterfaceIPv6Address("arc_eth0",
                          *net_base::IPv6Address::CreateFromString("fd00::1"));

  // Guest started.
  auto arc_dev =
      virtualdev(patchpanel::Client::GuestType::kArcContainer, "arc_eth0",
                 "eth0", net_base::IPv4Address(192, 168, 1, 1));
  net_base::IPv4Address addr4 =
      proxy_->root_ns_enabled_ ? arc_dev.host_ipv4_addr : ipv4_address_;
  net_base::IPv6Address addr6 =
      proxy_->root_ns_enabled_
          ? *net_base::IPv6Address::CreateFromString("fd00::1")
          : ipv6_address_;
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                          "arc_eth0", addr4.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                          "arc_eth0", addr6.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 arc_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);

  // Guest stopped.
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, arc_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_P(ProxyTest, ArcProxy_ListenForGuests) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  // Guest started.
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, ListenUDP(_, "arc_eth0")).WillOnce(Return(true));
    EXPECT_CALL(*new_resolver, ListenTCP(_, "arc_eth0")).WillOnce(Return(true));
  } else {
    EXPECT_CALL(*new_resolver, ListenUDP(_, _)).Times(0);
    EXPECT_CALL(*new_resolver, ListenTCP(_, _)).Times(0);
  }
  auto arc_dev = virtualdev(patchpanel::Client::GuestType::kArcContainer,
                            "arc_eth0", "eth0");
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 arc_dev);

  // Guest stopped.
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, StopListen(_, "arc_eth0")).Times(2);
  } else {
    EXPECT_CALL(*new_resolver, StopListen(_, _)).Times(0);
  }
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, arc_dev);
}

TEST_P(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherGuest) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  proxy_->ipv6_address_ = ipv6_address_;

  // Other guest started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0"));
}

TEST_P(ProxyTest, ArcProxy_NeverListenForOtherGuests) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  // Other guest started.
  EXPECT_CALL(*new_resolver, ListenUDP(_, _)).Times(0);
  EXPECT_CALL(*new_resolver, ListenTCP(_, _)).Times(0);
  auto plugin_vm_dev =
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap0", "eth0");
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 plugin_vm_dev);

  // Other guest stopped.
  EXPECT_CALL(*new_resolver, StopListen(_, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, plugin_vm_dev);
}

TEST_P(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherIfname) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"});
  proxy_->device_ = ShillDevice();
  SetListenAddresses(ipv4_address_, ipv6_address_);

  // ARC guest with other interface started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0"));
}

TEST_P(ProxyTest, ArcProxy_NeverListenForOtherIfname) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kWifi, "wlan0"));
  SetListenAddresses(ipv4_address_, ipv6_address_);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  // Other guest started.
  EXPECT_CALL(*new_resolver, ListenUDP(_, _)).Times(0);
  EXPECT_CALL(*new_resolver, ListenTCP(_, _)).Times(0);
  auto arc_dev = virtualdev(patchpanel::Client::GuestType::kArcContainer,
                            "arc_eth0", "eth0");
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 arc_dev);

  // Other guest stopped.
  EXPECT_CALL(*new_resolver, StopListen(_, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, arc_dev);
}

TEST_P(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Added) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  SetListenAddresses(ipv4_address_, /*ipv6_addr=*/std::nullopt);
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                          "arc_eth0", ipv6_address_.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, ListenUDP(_, "arc_eth0")).WillOnce(Return(true));
    EXPECT_CALL(*new_resolver, ListenTCP(_, "arc_eth0")).WillOnce(Return(true));
  }

  std::string ifname =
      proxy_->root_ns_enabled_ ? "arc_eth0" : proxy_->ns_.peer_ifname;
  int ifindex = proxy_->IfNameToIndex(ifname.c_str());
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeAdd, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, ifindex, AF_INET6);
  int scope = proxy_->root_ns_enabled_ ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
  msg.set_address_status(net_base::RTNLMessage::AddressStatus(0, 0, scope));
  msg.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg);
}

TEST_P(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Deleted) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  auto* new_resolver = new MockResolver();
  proxy_->resolver_ = base::WrapUnique(new_resolver);

  proxy_->lifeline_fds_.emplace(std::make_pair("arc_eth0", AF_INET6),
                                base::ScopedFD(make_fd()));

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));

  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*new_resolver, StopListen(AF_INET6, "arc_eth0")).Times(1);
  }

  std::string ifname =
      proxy_->root_ns_enabled_ ? "arc_eth0" : proxy_->ns_.peer_ifname;
  int ifindex = proxy_->IfNameToIndex(ifname.c_str());
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeDelete, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, ifindex, AF_INET6);
  int scope = proxy_->root_ns_enabled_ ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
  msg.set_address_status(net_base::RTNLMessage::AddressStatus(0, 0, scope));
  proxy_->RTNLMessageHandler(msg);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_P(ProxyTest, ArcProxy_SetDnsRedirectionRuleUnrelatedIPv6Added) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillRepeatedly(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);

  net_base::RTNLMessage msg_unrelated_ifindex(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_ifindex.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg_unrelated_ifindex.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_ifindex);

  net_base::RTNLMessage msg_unrelated_scope(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_scope.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_LINK));
  msg_unrelated_scope.SetAttribute(IFA_ADDRESS, ipv6_address_.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_scope);
}

TEST_P(ProxyTest, UpdateNameServers) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice());
  proxy_->device_->network_config.dns_servers = StringsToIPAddressesChecked(
      {// IPv4 name servers.
       "8.8.8.8", "192.168.1.1",
       // IPv6 name servers.
       "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16e", "::2"});
  proxy_->UpdateNameServers();

  const std::vector<net_base::IPv4Address> expected_ipv4_dns_addresses = {
      net_base::IPv4Address(8, 8, 8, 8), net_base::IPv4Address(192, 168, 1, 1)};
  const std::vector<net_base::IPv6Address> expected_ipv6_dns_addresses = {
      *net_base::IPv6Address::CreateFromString(
          "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16e"),
      *net_base::IPv6Address::CreateFromString("::2")};

  EXPECT_THAT(proxy_->doh_config_.ipv4_nameservers(),
              expected_ipv4_dns_addresses);
  EXPECT_THAT(proxy_->doh_config_.ipv6_nameservers(),
              expected_ipv6_dns_addresses);
}

TEST_P(ProxyTest, DomainDoHConfigsUpdate) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});

  std::vector<std::string> props = {"domain1.com", "domain2.net"};
  proxy_->OnDoHIncludedDomainsChanged(props);
  proxy_->OnDoHExcludedDomainsChanged(props);
}

TEST_P(ProxyTest, DomainDoHConfigsUpdate_ProxyStopped) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});
  proxy_->Stop();

  std::vector<std::string> props = {"domain1.com", "domain2.net"};
  proxy_->OnDoHIncludedDomainsChanged(props);
  proxy_->OnDoHExcludedDomainsChanged(props);
}

TEST_P(ProxyTest, ArcProxy_SetInterface) {
  SetUpProxy(GetParam(),
             Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto wifi = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                          shill::Client::Device::Type::kWifi, "wlan0",
                          {"8.8.8.8"}, {"2001:4860:4860::8888"});
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*resolver_, SetInterface("wlan0"));
  } else {
    EXPECT_CALL(*resolver_, SetInterface).Times(0);
  }
  proxy_->OnDeviceChanged(wifi.get());
}

TEST_P(ProxyTest, DefaultProxy_SetInterface) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kDefault});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8", "8.8.4.4"},
                         {"2001:4860:4860::8888", "2001:4860:4860::8844"});
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*resolver_, SetInterface("eth0"));
  } else {
    EXPECT_CALL(*resolver_, SetInterface).Times(0);
  }
  proxy_->OnDefaultDeviceChanged(dev.get());

  auto vpn = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kVPN);
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*resolver_, ClearInterface);
  } else {
    EXPECT_CALL(*resolver_, SetInterface).Times(0);
  }
  proxy_->OnDefaultDeviceChanged(vpn.get());
}

TEST_P(ProxyTest, SystemProxy_SetInterface) {
  SetUpProxy(GetParam(), Proxy::Options{.type = Proxy::Type::kSystem});
  SetListenAddresses(ipv4_address_, ipv6_address_);

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8", "8.8.4.4"},
                         {"2001:4860:4860::8888", "2001:4860:4860::8844"});
  if (proxy_->root_ns_enabled_) {
    EXPECT_CALL(*resolver_, SetInterface("eth0"));
  } else {
    EXPECT_CALL(*resolver_, SetInterface).Times(0);
  }
  proxy_->OnDefaultDeviceChanged(dev.get());
}
}  // namespace dns_proxy

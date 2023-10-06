// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <fcntl.h>
#include <linux/rtnetlink.h>
#include <sys/stat.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <chromeos/patchpanel/mock_message_dispatcher.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/byte_utils.h>
#include <net-base/rtnl_message.h>
#include <shill/dbus/client/fake_client.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxy-mocks.h>

#include "dns-proxy/ipc.pb.h"

namespace dns_proxy {
namespace {

constexpr net_base::IPv4Address kNetnsPeerIPv4Addr(100, 115, 92, 130);
constexpr net_base::IPv6Address kNetnsPeerIPv6Addr(
    0xfd, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01);
constexpr base::TimeDelta kRequestTimeout = base::Seconds(10000);
constexpr base::TimeDelta kRequestRetryDelay = base::Milliseconds(200);
constexpr int32_t kRequestMaxRetry = 1;

int make_fd() {
  std::string fn(
      ::testing::UnitTest::GetInstance()->current_test_info()->name());
  fn = "/tmp/" + fn;
  return open(fn.c_str(), O_CREAT, 0600);
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
    const std::string& phys_ifname) {
  patchpanel::Client::VirtualDevice device;
  device.ifname = ifname;
  device.phys_ifname = phys_ifname;
  device.guest_type = guest_type;
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

  MOCK_METHOD(bool, ListenUDP, (struct sockaddr*), (override));
  MOCK_METHOD(bool, ListenTCP, (struct sockaddr*), (override));
  MOCK_METHOD(void,
              SetNameServers,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void,
              SetDoHProviders,
              (const std::vector<std::string>&, bool),
              (override));
};

class TestProxy : public Proxy {
 public:
  TestProxy(const Options& opts,
            std::unique_ptr<patchpanel::Client> patchpanel,
            std::unique_ptr<shill::Client> shill,
            std::unique_ptr<patchpanel::MessageDispatcher<ProxyAddrMessage>>
                msg_dispatcher)
      : Proxy(opts,
              std::move(patchpanel),
              std::move(shill),
              std::move(msg_dispatcher)) {}

  std::unique_ptr<Resolver> resolver;
  std::unique_ptr<Resolver> NewResolver(base::TimeDelta timeout,
                                        base::TimeDelta retry_delay,
                                        int max_num_retries) override {
    return std::move(resolver);
  }
};

class ProxyTest : public ::testing::Test {
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

  void SetUpProxy(const Proxy::Options& opts,
                  std::unique_ptr<shill::Client::Device> device = nullptr,
                  bool set_resolver = true) {
    // Set up mocks and fakes.
    patchpanel_client_ = new MockPatchpanelClient();
    shill_client_ = new FakeShillClient(
        mock_bus_, reinterpret_cast<ManagerProxyInterface*>(
                       const_cast<ManagerProxyMock*>(&mock_manager_)));
    msg_dispatcher_ = new patchpanel::MockMessageDispatcher<ProxyAddrMessage>();

    // Initialize Proxy instance.
    proxy_ = std::make_unique<TestProxy>(
        opts, base::WrapUnique(patchpanel_client_),
        base::WrapUnique(shill_client_), base::WrapUnique(msg_dispatcher_));

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
    dev->ipconfig.ipv4_dns_addresses = ipv4_nameservers;
    dev->ipconfig.ipv6_dns_addresses = ipv6_nameservers;
    return dev;
  }

  void SetNamespaceAddresses(
      const std::optional<net_base::IPv4Address>& ipv4_addr,
      const std::optional<net_base::IPv6Address>& ipv6_addr) {
    proxy_->ns_fd_ = base::ScopedFD(make_fd());
    if (ipv4_addr) {
      proxy_->ns_.peer_ipv4_address = *ipv4_addr;
    }
    if (ipv6_addr) {
      proxy_->ns_peer_ipv6_address_ = *ipv6_addr;
    }
  }

  void SetNameServers(const std::vector<std::string>& ipv4_nameservers,
                      const std::vector<std::string>& ipv6_nameservers) {
    EXPECT_TRUE(proxy_->device_);
    proxy_->device_->ipconfig.ipv4_dns_addresses = ipv4_nameservers;
    proxy_->device_->ipconfig.ipv6_dns_addresses = ipv6_nameservers;
    proxy_->UpdateNameServers();
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  ManagerProxyMock mock_manager_;

  MockResolver* resolver_;
  patchpanel::MockMessageDispatcher<ProxyAddrMessage>* msg_dispatcher_;
  FakeShillClient* shill_client_;
  MockPatchpanelClient* patchpanel_client_;
  std::unique_ptr<TestProxy> proxy_;
};

TEST_F(ProxyTest, SystemProxy_OnShutdownClearsAddressPropertyOnShill) {
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _));
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  int unused;
  proxy_->OnShutdown(&unused);
}

TEST_F(ProxyTest, NonSystemProxy_OnShutdownDoesNotCallShill) {
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _)).Times(0);
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _)).Times(0);
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault}, ShillDevice());
  int unused;
  proxy_->OnShutdown(&unused);
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddressesDoesntCrashIfDieFalse) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_CALL(mock_manager_, SetProperty(_, _, _, _)).Times(0);
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  proxy_->SetShillDNSProxyAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr,
                                    /*die_on_failure=*/false,
                                    /*num_retries=*/0);
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddresses) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(mock_manager_,
              SetDNSProxyAddresses(ElementsAre(kNetnsPeerIPv4Addr.ToString(),
                                               kNetnsPeerIPv6Addr.ToString()),
                                   _, _))
      .WillOnce(Return(true));
  proxy_->SetShillDNSProxyAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddressesEmptyNameserver) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());

  // Only IPv4 nameserver.
  SetNameServers({"8.8.8.8"}, /*ipv6_nameservers=*/{});
  EXPECT_CALL(
      mock_manager_,
      SetDNSProxyAddresses(ElementsAre(kNetnsPeerIPv4Addr.ToString()), _, _))
      .WillOnce(Return(true));
  proxy_->SetShillDNSProxyAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Only IPv6 nameserver.
  SetNameServers(/*ipv4_nameservers=*/{}, {"2001:4860:4860::8888"});
  EXPECT_CALL(
      mock_manager_,
      SetDNSProxyAddresses(ElementsAre(kNetnsPeerIPv6Addr.ToString()), _, _))
      .WillOnce(Return(true));
  proxy_->SetShillDNSProxyAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);
}

TEST_F(ProxyTest, SystemProxy_ClearShillDNSProxyAddresses) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _));
  proxy_->ClearShillDNSProxyAddresses();
}

TEST_F(ProxyTest, SystemProxy_SendIPAddressesToController) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});

  ProxyAddrMessage msg;
  msg.set_type(ProxyAddrMessage::SET_ADDRS);
  msg.add_addrs(kNetnsPeerIPv4Addr.ToString());
  msg.add_addrs(kNetnsPeerIPv6Addr.ToString());
  EXPECT_CALL(*msg_dispatcher_, SendMessage(EqualsProto(msg)))
      .WillOnce(Return(true));
  proxy_->SendIPAddressesToController(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);
}

TEST_F(ProxyTest, SystemProxy_SendIPAddressesToControllerEmptyNameserver) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());

  // Only IPv4 nameserver.
  SetNameServers({"8.8.8.8"}, /*ipv6_nameservers=*/{});
  ProxyAddrMessage msg;
  msg.set_type(ProxyAddrMessage::SET_ADDRS);
  msg.add_addrs(kNetnsPeerIPv4Addr.ToString());
  EXPECT_CALL(*msg_dispatcher_, SendMessage(EqualsProto(msg)))
      .WillOnce(Return(true));
  proxy_->SendIPAddressesToController(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Only IPv6 nameserver.
  SetNameServers(/*ipv4_nameservers=*/{}, {"2001:4860:4860::8888"});
  msg.Clear();
  msg.set_type(ProxyAddrMessage::SET_ADDRS);
  msg.add_addrs(kNetnsPeerIPv6Addr.ToString());
  EXPECT_CALL(*msg_dispatcher_, SendMessage(EqualsProto(msg)))
      .WillOnce(Return(true));
  proxy_->SendIPAddressesToController(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);
}

TEST_F(ProxyTest, SystemProxy_ClearIPAddressesInController) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  EXPECT_CALL(*msg_dispatcher_, SendMessage(_)).WillOnce(Return(true));
  proxy_->ClearIPAddressesInController();
}

TEST_F(ProxyTest, ShillInitializedWhenReady) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});

  // Test class defaults to make shill client ready. Reset to false.
  proxy_->shill_ready_ = false;
  proxy_->OnShillReady(true);
  EXPECT_TRUE(proxy_->shill_ready_);
}

TEST_F(ProxyTest, SystemProxy_ConnectedNamedspace) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});

  EXPECT_CALL(
      *patchpanel_client_,
      ConnectNamespace(_, _, /*outbound_ifname=*/"", /*route_on_vpn=*/false,
                       patchpanel::Client::TrafficSource::kSystem, _))
      .WillOnce(
          Return(std::make_pair(base::ScopedFD(make_fd()),
                                patchpanel::Client::ConnectedNamespace{})));
  proxy_->OnPatchpanelReady(true);
}

TEST_F(ProxyTest, DefaultProxy_ConnectedNamedspace) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault}, ShillDevice());

  EXPECT_CALL(
      *patchpanel_client_,
      ConnectNamespace(_, _, /*outbound_ifname=*/"", /*route_on_vpn=*/true,
                       patchpanel::Client::TrafficSource::kUser, _))
      .WillOnce(
          Return(std::make_pair(base::ScopedFD(make_fd()),
                                patchpanel::Client::ConnectedNamespace{})));
  proxy_->OnPatchpanelReady(true);
}

TEST_F(ProxyTest, ArcProxy_ConnectedNamedspace) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"});

  EXPECT_CALL(*patchpanel_client_,
              ConnectNamespace(_, _, /*outbound_ifname=*/"eth0",
                               /*route_on_vpn=*/false,
                               patchpanel::Client::TrafficSource::kArc, _))
      .WillOnce(
          Return(std::make_pair(base::ScopedFD(make_fd()),
                                patchpanel::Client::ConnectedNamespace{})));
  proxy_->OnPatchpanelReady(true);
}

TEST_F(ProxyTest, ShillResetRestoresAddressProperty) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  EXPECT_CALL(mock_manager_,
              SetDNSProxyAddresses(ElementsAre(kNetnsPeerIPv4Addr.ToString(),
                                               kNetnsPeerIPv6Addr.ToString()),
                                   _, _))
      .WillOnce(Return(true));
  proxy_->OnShillReset(true);
}

TEST_F(ProxyTest, StateClearedIfDefaultServiceDrops) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());

  proxy_->OnDefaultDeviceChanged(nullptr /* no service */);
  EXPECT_FALSE(proxy_->device_);
  EXPECT_FALSE(proxy_->resolver_);
}

TEST_F(ProxyTest, ArcProxy_IgnoredIfDefaultServiceDrops) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());

  proxy_->OnDefaultDeviceChanged(nullptr /* no service */);
  EXPECT_TRUE(proxy_->device_);
  EXPECT_TRUE(proxy_->resolver_);
}

TEST_F(ProxyTest, StateClearedIfDefaultServiceIsNotOnline) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kReady);
  proxy_->OnDefaultDeviceChanged(dev.get());

  EXPECT_FALSE(proxy_->device_);
  EXPECT_FALSE(proxy_->resolver_);
}

TEST_F(ProxyTest, NewResolverStartsListeningOnDefaultServiceComesOnline) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault},
             /*device=*/nullptr, /*set_resolver=*/false);

  auto* new_resolver = new MockResolver();
  proxy_->resolver = base::WrapUnique(new_resolver);
  EXPECT_CALL(*new_resolver, ListenUDP(_)).WillOnce(Return(true));
  EXPECT_CALL(*new_resolver, ListenTCP(_)).WillOnce(Return(true));

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline);
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  proxy_->OnDefaultDeviceChanged(dev.get());
  EXPECT_TRUE(proxy_->resolver_);
}

TEST_F(ProxyTest, NameServersUpdatedOnDefaultServiceComesOnline) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault});

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

TEST_F(ProxyTest, SystemProxy_ShillPropertyUpdatedOnDefaultServiceComesOnline) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline);
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  proxy_->OnDefaultDeviceChanged(dev.get());
}

TEST_F(ProxyTest, SystemProxy_IgnoresVPN) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

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

TEST_F(ProxyTest, SystemProxy_GetsPhysicalDeviceOnInitialVPN) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  shill_client_->default_device_ =
      ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                  shill::Client::Device::Type::kWifi);

  auto vpn = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kVPN);
  proxy_->OnDefaultDeviceChanged(vpn.get());
  EXPECT_TRUE(proxy_->device_);
  EXPECT_EQ(proxy_->device_->type, shill::Client::Device::Type::kWifi);
}

TEST_F(ProxyTest, DefaultProxy_UsesVPN) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault});

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

TEST_F(ProxyTest, ArcProxy_NameServersUpdatedOnDeviceChangeEvent) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"});

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
  wifi->ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  wifi->ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                       "2001:4860:4860::8844"};
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy_->OnDeviceChanged(wifi.get());
}

TEST_F(ProxyTest, SystemProxy_NameServersUpdatedOnDeviceChangeEvent) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Set name servers on device change event.
  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0",
                         {"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"),
                                         StrEq("2001:4860:4860::8888"))));
  proxy_->OnDefaultDeviceChanged(dev.get());

  // Now trigger an ipconfig change.
  dev->ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  dev->ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                      "2001:4860:4860::8844"};
  EXPECT_CALL(*resolver_,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy_->OnDeviceChanged(dev.get());
}

TEST_F(ProxyTest, DeviceChangeEventIgnored) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0");

  // Set name servers on device change event.
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(1);
  proxy_->OnDefaultDeviceChanged(dev.get());

  // No change to ipconfig, no call to SetNameServers
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(0);
  proxy_->OnDeviceChanged(dev.get());

  // Different ifname, no call to SetNameServers
  EXPECT_CALL(*resolver_, SetNameServers(_)).Times(0);
  dev->ifname = "eth1";
  proxy_->OnDeviceChanged(dev.get());
}

TEST_F(ProxyTest, BasicDoHDisable) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  brillo::VariantDictionary props;
  proxy_->OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, BasicDoHAlwaysOn) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), true));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, BasicDoHAutomatic) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"8.8.4.4"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, RemovesDNSQueryParameterTemplate_AlwaysOn) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), true));
  brillo::VariantDictionary props;
  props["https://dns.google.com{?dns}"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, RemovesDNSQueryParameterTemplate_Automatic) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));
  SetNameServers({"8.8.4.4"}, /*ipv6_nameservers=*/{});

  EXPECT_CALL(
      *resolver_,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com{?dns}"] = std::string("8.8.8.8, 8.8.4.4");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, NewResolverConfiguredWhenSet) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
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

TEST_F(ProxyTest, DoHModeChangingFixedNameServers) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
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

TEST_F(ProxyTest, MultipleDoHProvidersForAlwaysOnMode) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
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

TEST_F(ProxyTest, MultipleDoHProvidersForAutomaticMode) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline));

  shill::Client::IPConfig ipconfig;
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

TEST_F(ProxyTest, DoHBadAlwaysOnConfigSetsAutomaticMode) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem},
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

TEST_F(ProxyTest, DefaultProxy_DisableDoHProvidersOnVPN) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault});
  proxy_->device_ = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                                shill::Client::Device::Type::kVPN);

  EXPECT_CALL(*resolver_, SetDoHProviders(IsEmpty(), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  props["https://doh.opendns.com"] = std::string("");
  proxy_->OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, SystemProxy_SetsDnsRedirectionRule) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // System proxy requests a DnsRedirectionRule to exclude traffic destined
  // not to the underlying network's name server.
  auto dev = ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0");
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(
          patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination, _,
          kNetnsPeerIPv4Addr.ToString(), _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(
          patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination, _,
          kNetnsPeerIPv6Addr.ToString(), _, _))
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

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleDeviceAlreadyStarted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault}, ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Set DNS redirection rule.
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("8.8.8.8"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, _, ElementsAre("2001:4860:4860::8888"), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->Enable();
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleNewDeviceStarted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Empty active device.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->Enable();
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

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleGuest) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Guest started.
  auto plugin_vm_dev =
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap0", "eth0");
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", kNetnsPeerIPv4Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", kNetnsPeerIPv6Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);

  // Guest stopped.
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, DefaultProxy_NeverSetsDnsRedirectionRuleOtherGuest) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault},
             ShillDevice(shill::Client::Device::ConnectionState::kOnline,
                         shill::Client::Device::Type::kEthernet, "eth0"));
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Other guest started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kArcContainer, "arc_eth0",
                 "eth0"));
}

TEST_F(ProxyTest, SystemProxy_SetDnsRedirectionRuleIPv6Added) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, /*ipv6_addr=*/std::nullopt);

  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(
          patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination, _,
          kNetnsPeerIPv6Addr.ToString(), _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to 0 in order to match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeAdd, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                            AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg);
}

TEST_F(ProxyTest, SystemProxy_SetDnsRedirectionRuleIPv6Deleted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());

  proxy_->lifeline_fds_.emplace(std::make_pair("", AF_INET6),
                                base::ScopedFD(make_fd()));

  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeDelete, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                            AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  proxy_->RTNLMessageHandler(msg);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleWithoutIPv6) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, /*ipv6_addr=*/std::nullopt);

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
      virtualdev(patchpanel::Client::GuestType::kParallelsVm, "vmtap0", "eth0");
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", kNetnsPeerIPv4Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);

  // Guest stopped.
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, plugin_vm_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 1);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Added) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault}, ShillDevice());
  SetNameServers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, /*ipv6_addr=*/std::nullopt);

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0")}));
  EXPECT_CALL(*patchpanel_client_,
              RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kUser,
                          _, kNetnsPeerIPv6Addr.ToString(), _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kDefault,
                  "vmtap0", kNetnsPeerIPv6Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to 0 in order to match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeAdd, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                            AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Deleted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault}, ShillDevice());

  proxy_->lifeline_fds_.emplace(std::make_pair("", AF_INET6),
                                base::ScopedFD(make_fd()));
  proxy_->lifeline_fds_.emplace(std::make_pair("vmtap0", AF_INET6),
                                base::ScopedFD(make_fd()));

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0")}));

  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeDelete, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                            AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  proxy_->RTNLMessageHandler(msg);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleUnrelatedIPv6Added) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kDefault}, ShillDevice());

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillRepeatedly(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0")}));
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to -1 in order to not match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  net_base::RTNLMessage msg_unrelated_ifindex(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_ifindex.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg_unrelated_ifindex.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_ifindex);

  net_base::RTNLMessage msg_unrelated_scope(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_scope.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_LINK));
  msg_unrelated_scope.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_scope);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleDeviceAlreadyStarted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Set devices created before the proxy started.
  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                  "arc_eth0", kNetnsPeerIPv4Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                  "arc_eth0", kNetnsPeerIPv6Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->Enable();
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleNewDeviceStarted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // Guest started.
  auto arc_dev = virtualdev(patchpanel::Client::GuestType::kArcContainer,
                            "arc_eth0", "eth0");
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                  "arc_eth0", kNetnsPeerIPv4Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                  "arc_eth0", kNetnsPeerIPv6Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy_->OnVirtualDeviceChanged(patchpanel::Client::VirtualDeviceEvent::kAdded,
                                 arc_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 2);

  // Guest stopped.
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kRemoved, arc_dev);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherGuest) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  proxy_->ns_peer_ipv6_address_ = kNetnsPeerIPv6Addr;

  // Other guest started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kTerminaVm, "vmtap0", "eth0"));
}

TEST_F(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherIfname) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"});
  proxy_->device_ = ShillDevice();
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, kNetnsPeerIPv6Addr);

  // ARC guest with other interface started.
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);
  proxy_->OnVirtualDeviceChanged(
      patchpanel::Client::VirtualDeviceEvent::kAdded,
      virtualdev(patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0"));
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Added) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());
  SetNamespaceAddresses(kNetnsPeerIPv4Addr, /*ipv6_addr=*/std::nullopt);

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));
  EXPECT_CALL(
      *patchpanel_client_,
      RedirectDns(patchpanel::Client::DnsRedirectionRequestType::kArc,
                  "arc_eth0", kNetnsPeerIPv6Addr.ToString(), IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to 0 in order to match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeAdd, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                            AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Deleted) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());

  proxy_->lifeline_fds_.emplace(std::make_pair("arc_eth0", AF_INET6),
                                base::ScopedFD(make_fd()));

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillOnce(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));

  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeAddress,
                            net_base::RTNLMessage::kModeDelete, 0 /* flags */,
                            0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                            AF_INET6);
  msg.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  proxy_->RTNLMessageHandler(msg);
  EXPECT_EQ(proxy_->lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleUnrelatedIPv6Added) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
             ShillDevice());

  EXPECT_CALL(*patchpanel_client_, GetDevices())
      .WillRepeatedly(
          Return(std::vector<patchpanel::Client::VirtualDevice>{virtualdev(
              patchpanel::Client::GuestType::kArcVm, "arc_eth0", "eth0")}));
  EXPECT_CALL(*patchpanel_client_, RedirectDns(_, _, _, _, _)).Times(0);

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to -1 in order to not match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  net_base::RTNLMessage msg_unrelated_ifindex(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_ifindex.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg_unrelated_ifindex.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_ifindex);

  net_base::RTNLMessage msg_unrelated_scope(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      AF_INET6);
  msg_unrelated_scope.set_address_status(
      net_base::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_LINK));
  msg_unrelated_scope.SetAttribute(IFA_ADDRESS, kNetnsPeerIPv6Addr.ToBytes());
  proxy_->RTNLMessageHandler(msg_unrelated_scope);
}

TEST_F(ProxyTest, UpdateNameServers) {
  SetUpProxy(Proxy::Options{.type = Proxy::Type::kSystem}, ShillDevice());
  proxy_->device_->ipconfig.ipv4_dns_addresses = {
      // Valid IPv4 name servers.
      "8.8.8.8", "192.168.1.1",
      // Valid IPv6 name servers inside IPv4 config.
      // Expected to be propagated to DNS proxy's
      // IPv6 name servers.
      "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16d", "::1",
      // Ignored invalid name servers.
      "256.256.256.256", "0.0.0.0", "::", "a", ""};
  proxy_->device_->ipconfig.ipv6_dns_addresses = {
      // Ignored valid IPv4 name servers.
      "8.8.4.4", "192.168.1.2",
      // Valid IPv6 name servers.
      "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16e", "::2",
      // Ignored invalid name servers.
      "256.256.256.257", "0.0.0.0", "::", "b", ""};
  proxy_->UpdateNameServers();

  const std::vector<net_base::IPv4Address> expected_ipv4_dns_addresses = {
      net_base::IPv4Address(8, 8, 8, 8), net_base::IPv4Address(192, 168, 1, 1)};
  const std::vector<net_base::IPv6Address> expected_ipv6_dns_addresses = {
      *net_base::IPv6Address::CreateFromString(
          "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16d"),
      *net_base::IPv6Address::CreateFromString("::1"),
      *net_base::IPv6Address::CreateFromString(
          "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16e"),
      *net_base::IPv6Address::CreateFromString("::2")};

  EXPECT_THAT(proxy_->doh_config_.ipv4_nameservers(),
              expected_ipv4_dns_addresses);
  EXPECT_THAT(proxy_->doh_config_.ipv6_nameservers(),
              expected_ipv6_dns_addresses);
}
}  // namespace dns_proxy

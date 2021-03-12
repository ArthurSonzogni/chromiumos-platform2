// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <memory>
#include <utility>
#include <vector>

#include <chromeos/patchpanel/net_util.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus/client/fake_client.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxy-mocks.h>

namespace dns_proxy {
namespace {
constexpr base::TimeDelta kRequestTimeout = base::TimeDelta::FromSeconds(10000);
constexpr base::TimeDelta kRequestRetryDelay =
    base::TimeDelta::FromMilliseconds(200);
constexpr int32_t kRequestMaxRetry = 1;

}  // namespace
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ManagerProxyMock;
using testing::_;
using testing::IsEmpty;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;

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

  bool IsInitialized() const { return init_; }

  std::unique_ptr<shill::Client::Device> default_device_;

 private:
  ManagerProxyInterface* manager_proxy_;
};

class FakePatchpanelClient : public patchpanel::FakeClient {
 public:
  FakePatchpanelClient() = default;
  ~FakePatchpanelClient() = default;

  void SetConnectNamespaceResult(
      int fd, const patchpanel::ConnectNamespaceResponse& resp) {
    ns_fd_ = fd;
    ns_resp_ = resp;
  }

  std::pair<base::ScopedFD, patchpanel::ConnectNamespaceResponse>
  ConnectNamespace(pid_t pid,
                   const std::string& outbound_ifname,
                   bool forward_user_traffic,
                   bool route_on_vpn,
                   patchpanel::TrafficCounter::Source traffic_source) override {
    ns_ifname_ = outbound_ifname;
    ns_rvpn_ = route_on_vpn;
    ns_ts_ = traffic_source;
    return {base::ScopedFD(ns_fd_), ns_resp_};
  }

  std::string ns_ifname_;
  bool ns_rvpn_;
  patchpanel::TrafficCounter::Source ns_ts_;
  int ns_fd_;
  patchpanel::ConnectNamespaceResponse ns_resp_;
};

class MockResolver : public Resolver {
 public:
  MockResolver()
      : Resolver(kRequestTimeout, kRequestRetryDelay, kRequestMaxRetry) {}
  ~MockResolver() = default;

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
            std::unique_ptr<shill::Client> shill)
      : Proxy(opts, std::move(patchpanel), std::move(shill)) {}

  std::unique_ptr<Resolver> resolver;
  std::unique_ptr<Resolver> NewResolver(base::TimeDelta timeout,
                                        base::TimeDelta retry_delay,
                                        int max_num_retries) override {
    return std::move(resolver);
  }
};

class ProxyTest : public ::testing::Test {
 protected:
  ProxyTest() : mock_bus_(new dbus::MockBus{dbus::Bus::Options{}}) {}
  ~ProxyTest() { mock_bus_->ShutdownAndBlock(); }

  std::unique_ptr<FakePatchpanelClient> PatchpanelClient() const {
    return std::make_unique<FakePatchpanelClient>();
  }

  std::unique_ptr<FakeShillClient> ShillClient() const {
    return std::make_unique<FakeShillClient>(
        mock_bus_, reinterpret_cast<ManagerProxyInterface*>(
                       const_cast<ManagerProxyMock*>(&mock_manager_)));
  }

  int make_fd() const {
    std::string fn(
        ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fn = "/tmp/" + fn;
    return open(fn.c_str(), O_CREAT, 0600);
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  ManagerProxyMock mock_manager_;
};

TEST_F(ProxyTest, SystemProxy_OnShutdownClearsAddressPropertyOnShill) {
  EXPECT_CALL(mock_manager_, SetProperty(shill::kDNSProxyIPv4AddressProperty,
                                         brillo::Any(std::string()), _, _))
      .WillOnce(Return(true));
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  int unused;
  proxy.OnShutdown(&unused);
}

TEST_F(ProxyTest, NonSystemProxy_OnShutdownDoesNotCallShill) {
  EXPECT_CALL(mock_manager_, SetProperty(_, _, _, _)).Times(0);
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, PatchpanelClient(),
              ShillClient());
  int unused;
  proxy.OnShutdown(&unused);
}

TEST_F(ProxyTest, SystemProxy_SetShillPropertyWithNoRetriesCrashes) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  EXPECT_DEATH(proxy.SetShillProperty("10.10.10.10", true, 0), "");
}

TEST_F(ProxyTest, SystemProxy_SetShillPropertyDoesntCrashIfDieFalse) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_CALL(mock_manager_, SetProperty(_, _, _, _)).Times(0);
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.SetShillProperty("10.10.10.10", false, 0);
}

TEST_F(ProxyTest, ShillInitializedWhenReady) {
  auto shill = ShillClient();
  auto* shill_ptr = shill.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              std::move(shill));
  proxy.OnShillReady(true);
  EXPECT_TRUE(shill_ptr->IsInitialized());
}

TEST_F(ProxyTest, SystemProxy_ConnectedNamedspace) {
  auto pp = PatchpanelClient();
  auto* pp_ptr = pp.get();
  pp->SetConnectNamespaceResult(make_fd(),
                                patchpanel::ConnectNamespaceResponse());
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, std::move(pp),
              ShillClient());
  proxy.OnPatchpanelReady(true);
  EXPECT_TRUE(pp_ptr->ns_ifname_.empty());
  EXPECT_FALSE(pp_ptr->ns_rvpn_);
  EXPECT_EQ(pp_ptr->ns_ts_, patchpanel::TrafficCounter::SYSTEM);
}

TEST_F(ProxyTest, DefaultProxy_ConnectedNamedspace) {
  auto pp = PatchpanelClient();
  auto* pp_ptr = pp.get();
  pp->SetConnectNamespaceResult(make_fd(),
                                patchpanel::ConnectNamespaceResponse());
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(pp),
              ShillClient());
  proxy.OnPatchpanelReady(true);
  EXPECT_TRUE(pp_ptr->ns_ifname_.empty());
  EXPECT_TRUE(pp_ptr->ns_rvpn_);
  EXPECT_EQ(pp_ptr->ns_ts_, patchpanel::TrafficCounter::USER);
}

TEST_F(ProxyTest, ArcProxy_ConnectedNamedspace) {
  auto pp = PatchpanelClient();
  auto* pp_ptr = pp.get();
  pp->SetConnectNamespaceResult(make_fd(),
                                patchpanel::ConnectNamespaceResponse());
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(pp), ShillClient());
  proxy.OnPatchpanelReady(true);
  EXPECT_EQ(pp_ptr->ns_ifname_, "eth0");
  EXPECT_FALSE(pp_ptr->ns_rvpn_);
  EXPECT_EQ(pp_ptr->ns_ts_, patchpanel::TrafficCounter::ARC);
}

TEST_F(ProxyTest, CrashOnConnectNamespaceFailure) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  auto pp = PatchpanelClient();
  pp->SetConnectNamespaceResult(-1 /* invalid fd */,
                                patchpanel::ConnectNamespaceResponse());
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(pp), ShillClient());
  EXPECT_DEATH(proxy.OnPatchpanelReady(true), "namespace");
}

TEST_F(ProxyTest, CrashOnPatchpanelNotReady) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              PatchpanelClient(), ShillClient());
  EXPECT_DEATH(proxy.OnPatchpanelReady(false), "patchpanel");
}

TEST_F(ProxyTest, ShillResetRestoresAddressProperty) {
  auto pp = PatchpanelClient();
  patchpanel::ConnectNamespaceResponse resp;
  resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
  pp->SetConnectNamespaceResult(make_fd(), resp);
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, std::move(pp),
              ShillClient());
  proxy.OnPatchpanelReady(true);
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty,
                          brillo::Any(std::string("10.10.10.10")), _, _))
      .WillOnce(Return(true));
  proxy.OnShillReset(true);
}

TEST_F(ProxyTest, StateClearedIfDefaultServiceDrops) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.OnDefaultDeviceChanged(nullptr /* no service */);
  EXPECT_FALSE(proxy.device_);
  EXPECT_FALSE(proxy.resolver_);
}

TEST_F(ProxyTest, ArcProxy_IgnoredIfDefaultServiceDrops) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.OnDefaultDeviceChanged(nullptr /* no service */);
  EXPECT_TRUE(proxy.device_);
  EXPECT_TRUE(proxy.resolver_);
}

TEST_F(ProxyTest, StateClearedIfDefaultServiceIsNotOnline) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  proxy.resolver_ = std::make_unique<MockResolver>();
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kReady;
  proxy.OnDefaultDeviceChanged(&dev);
  EXPECT_FALSE(proxy.device_);
  EXPECT_FALSE(proxy.resolver_);
}

TEST_F(ProxyTest, NewResolverStartsListeningOnDefaultServiceComesOnline) {
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kDefault},
                  PatchpanelClient(), ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver = std::move(resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).WillOnce(Return(true));
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  proxy.OnDefaultDeviceChanged(&dev);
  EXPECT_TRUE(proxy.resolver_);
}

TEST_F(ProxyTest, CrashOnListenFailure) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kSystem},
                  PatchpanelClient(), ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver = std::move(resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  ON_CALL(*mock_resolver, ListenUDP(_)).WillByDefault(Return(false));
  ON_CALL(*mock_resolver, ListenTCP(_)).WillByDefault(Return(false));
  EXPECT_DEATH(proxy.OnDefaultDeviceChanged(&dev), "relay loop");
}

TEST_F(ProxyTest, NameServersUpdatedOnDefaultServiceComesOnline) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"a", "b"};
  dev.ipconfig.ipv6_dns_addresses = {"c", "d"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(
                  ElementsAre(StrEq("a"), StrEq("b"), StrEq("c"), StrEq("d"))));
  proxy.OnDefaultDeviceChanged(&dev);
}

TEST_F(ProxyTest, SystemProxy_ShillPropertyUpdatedOnDefaultServiceComesOnline) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  EXPECT_CALL(*mock_resolver, SetNameServers(_));
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty, _, _, _))
      .WillOnce(Return(true));
  proxy.OnDefaultDeviceChanged(&dev);
}

TEST_F(ProxyTest, SystemProxy_IgnoresVPN) {
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kSystem},
                  PatchpanelClient(), ShillClient());
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  ON_CALL(*mock_resolver, ListenUDP(_)).WillByDefault(Return(true));
  ON_CALL(*mock_resolver, ListenTCP(_)).WillByDefault(Return(true));
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty, _, _, _))
      .WillOnce(Return(true));
  proxy.resolver = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.type = shill::Client::Device::Type::kWifi;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  proxy.OnDefaultDeviceChanged(&dev);
  EXPECT_TRUE(proxy.device_);
  EXPECT_EQ(proxy.device_->type, shill::Client::Device::Type::kWifi);
  dev.type = shill::Client::Device::Type::kVPN;
  proxy.OnDefaultDeviceChanged(&dev);
  EXPECT_TRUE(proxy.device_);
  EXPECT_EQ(proxy.device_->type, shill::Client::Device::Type::kWifi);
}

TEST_F(ProxyTest, SystemProxy_GetsPhysicalDeviceOnInitialVPN) {
  auto shill = ShillClient();
  auto* shill_ptr = shill.get();
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kSystem},
                  PatchpanelClient(), std::move(shill));
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  ON_CALL(*mock_resolver, ListenUDP(_)).WillByDefault(Return(true));
  ON_CALL(*mock_resolver, ListenTCP(_)).WillByDefault(Return(true));
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty, _, _, _))
      .WillOnce(Return(true));
  proxy.resolver = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device vpn;
  vpn.type = shill::Client::Device::Type::kVPN;
  vpn.state = shill::Client::Device::ConnectionState::kOnline;
  shill_ptr->default_device_ = std::make_unique<shill::Client::Device>();
  shill_ptr->default_device_->type = shill::Client::Device::Type::kWifi;
  shill_ptr->default_device_->state =
      shill::Client::Device::ConnectionState::kOnline;
  proxy.OnDefaultDeviceChanged(&vpn);
  EXPECT_TRUE(proxy.device_);
  EXPECT_EQ(proxy.device_->type, shill::Client::Device::Type::kWifi);
}

TEST_F(ProxyTest, DefaultProxy_UsesVPN) {
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kDefault},
                  PatchpanelClient(), ShillClient());
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  ON_CALL(*mock_resolver, ListenUDP(_)).WillByDefault(Return(true));
  ON_CALL(*mock_resolver, ListenTCP(_)).WillByDefault(Return(true));
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  proxy.resolver = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.type = shill::Client::Device::Type::kWifi;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  proxy.OnDefaultDeviceChanged(&dev);
  EXPECT_TRUE(proxy.device_);
  EXPECT_EQ(proxy.device_->type, shill::Client::Device::Type::kWifi);
  dev.type = shill::Client::Device::Type::kVPN;
  proxy.OnDefaultDeviceChanged(&dev);
  EXPECT_TRUE(proxy.device_);
  EXPECT_EQ(proxy.device_->type, shill::Client::Device::Type::kVPN);
}

TEST_F(ProxyTest, NameServersUpdatedOnDeviceChangeEvent) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"a", "b"};
  dev.ipconfig.ipv6_dns_addresses = {"c", "d"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(
                  ElementsAre(StrEq("a"), StrEq("b"), StrEq("c"), StrEq("d"))));
  proxy.OnDefaultDeviceChanged(&dev);

  // Now trigger an ipconfig change.
  dev.ipconfig.ipv4_dns_addresses = {"X"};
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("X"), StrEq("c"), StrEq("d"))));
  proxy.OnDeviceChanged(&dev);
}

TEST_F(ProxyTest, DeviceChangeEventIgnored) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.ifname = "eth0";
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"a", "b"};
  dev.ipconfig.ipv6_dns_addresses = {"c", "d"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(
                  ElementsAre(StrEq("a"), StrEq("b"), StrEq("c"), StrEq("d"))));
  proxy.OnDefaultDeviceChanged(&dev);

  // No change to ipconfig, no call to SetNameServers
  proxy.OnDeviceChanged(&dev);

  // Different ifname, no call to SetNameServers
  dev.ifname = "wlan0";
  proxy.OnDeviceChanged(&dev);
}

TEST_F(ProxyTest, BasicDoHDisable) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  brillo::VariantDictionary props;
  proxy.OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, BasicDoHAlwaysOn) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), true));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  proxy.OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, BasicDoHAutomatic) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::IPConfig ipconfig;
  ipconfig.ipv4_dns_addresses = {"8.8.4.4"};
  proxy.UpdateNameServers(ipconfig);

  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  proxy.OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, NewResolverConfiguredWhenSet) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  props["https://chrome.cloudflare-dns.com/dns-query"] =
      std::string("1.1.1.1,2606:4700:4700::1111");
  proxy.OnDoHProvidersChanged(props);
  shill::Client::IPConfig ipconfig;
  ipconfig.ipv4_dns_addresses = {"1.0.0.1", "1.1.1.1"};
  proxy.UpdateNameServers(ipconfig);

  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  EXPECT_CALL(*mock_resolver, SetNameServers(UnorderedElementsAre(
                                  StrEq("1.1.1.1"), StrEq("1.0.0.1"))));
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(
          ElementsAre(StrEq("https://chrome.cloudflare-dns.com/dns-query")),
          false));
  proxy.doh_config_.set_resolver(mock_resolver);
}

TEST_F(ProxyTest, DoHModeChangingFixedNameServers) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);

  // Initially off.
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  shill::Client::IPConfig ipconfig;
  ipconfig.ipv4_dns_addresses = {"1.1.1.1", "9.9.9.9"};
  proxy.UpdateNameServers(ipconfig);

  // Automatic mode - matched cloudflare.
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(
          ElementsAre(StrEq("https://chrome.cloudflare-dns.com/dns-query")),
          false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("8.8.8.8, 8.8.4.4");
  props["https://chrome.cloudflare-dns.com/dns-query"] =
      std::string("1.1.1.1,2606:4700:4700::1111");
  proxy.OnDoHProvidersChanged(props);

  // Automatic mode - no match.
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  ipconfig.ipv4_dns_addresses = {"10.10.10.1"};
  proxy.UpdateNameServers(ipconfig);

  // Automatic mode - matched google.
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(ElementsAre(StrEq("https://dns.google.com")), false));
  ipconfig.ipv4_dns_addresses = {"8.8.4.4", "10.10.10.1", "8.8.8.8"};
  proxy.UpdateNameServers(ipconfig);

  // Explicitly turned off.
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  props.clear();
  proxy.OnDoHProvidersChanged(props);

  // Still off - even switching ns back.
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  ipconfig.ipv4_dns_addresses = {"8.8.4.4", "10.10.10.1", "8.8.8.8"};
  proxy.UpdateNameServers(ipconfig);

  // Always-on mode.
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(ElementsAre(StrEq("https://doh.opendns.com/dns-query")),
                      true));
  props.clear();
  props["https://doh.opendns.com/dns-query"] = std::string("");
  proxy.OnDoHProvidersChanged(props);

  // Back to automatic mode, though no matching ns.
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  props.clear();
  props["https://doh.opendns.com/dns-query"] = std::string(
      "208.67.222.222,208.67.220.220,2620:119:35::35, 2620:119:53::53");
  proxy.OnDoHProvidersChanged(props);

  // Automatic mode working on ns update.
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(ElementsAre(StrEq("https://doh.opendns.com/dns-query")),
                      false));
  ipconfig.ipv4_dns_addresses = {"8.8.8.8", "2620:119:35::35"};
  proxy.UpdateNameServers(ipconfig);
}

TEST_F(ProxyTest, MultipleDoHProvidersForAlwaysOnMode) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  EXPECT_CALL(
      *mock_resolver,
      SetDoHProviders(UnorderedElementsAre(StrEq("https://dns.google.com"),
                                           StrEq("https://doh.opendns.com")),
                      true));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  props["https://doh.opendns.com"] = std::string("");
  proxy.OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, MultipleDoHProvidersForAutomaticMode) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::IPConfig ipconfig;
  ipconfig.ipv4_dns_addresses = {"1.1.1.1", "10.10.10.10"};
  proxy.UpdateNameServers(ipconfig);

  EXPECT_CALL(
      *mock_resolver,
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
  proxy.OnDoHProvidersChanged(props);

  EXPECT_CALL(*mock_resolver,
              SetDoHProviders(UnorderedElementsAre(
                                  StrEq("https://dns.google.com"),
                                  StrEq("https://doh.opendns.com/dns-query"),
                                  StrEq("https://dns.quad9.net/dns-query")),
                              false));
  ipconfig.ipv4_dns_addresses = {"8.8.8.8", "10.10.10.10"};
  ipconfig.ipv6_dns_addresses = {"2620:fe::9", "2620:119:53::53"};
  proxy.UpdateNameServers(ipconfig);
}

TEST_F(ProxyTest, DoHBadAlwaysOnConfigSetsAutomaticMode) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::IPConfig ipconfig;
  ipconfig.ipv4_dns_addresses = {"1.1.1.1", "10.10.10.10"};
  proxy.UpdateNameServers(ipconfig);

  EXPECT_CALL(
      *mock_resolver,
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
  proxy.OnDoHProvidersChanged(props);

  EXPECT_CALL(*mock_resolver,
              SetDoHProviders(UnorderedElementsAre(
                                  StrEq("https://dns.google.com"),
                                  StrEq("https://doh.opendns.com/dns-query"),
                                  StrEq("https://dns.quad9.net/dns-query")),
                              false));
  ipconfig.ipv4_dns_addresses = {"8.8.8.8", "10.10.10.10"};
  ipconfig.ipv6_dns_addresses = {"2620:fe::9", "2620:119:53::53"};
  proxy.UpdateNameServers(ipconfig);
}

}  // namespace dns_proxy

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <fcntl.h>
#include <linux/rtnetlink.h>
#include <sys/stat.h>

#include <memory>
#include <utility>
#include <vector>

#include <chromeos/patchpanel/net_util.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus/client/fake_client.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxy-mocks.h>
#include <shill/net/rtnl_handler.h>

using testing::ElementsAreArray;

namespace dns_proxy {
namespace {
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

  base::ScopedFD RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::RuleType,
                             const std::string&,
                             const std::string&,
                             const std::vector<std::string>&,
                             const std::string&) override {
    return base::ScopedFD(make_fd());
  }

  std::string ns_ifname_;
  bool ns_rvpn_;
  patchpanel::TrafficCounter::Source ns_ts_;
  int ns_fd_;
  patchpanel::ConnectNamespaceResponse ns_resp_;
};

class FakeFeaturesClient : public ChromeFeaturesServiceClient {
 public:
  explicit FakeFeaturesClient(bool enabled)
      : ChromeFeaturesServiceClient(nullptr), enabled_(enabled) {}
  void IsDNSProxyEnabled(
      ChromeFeaturesServiceClient::IsFeatureEnabledCallback callback) override {
    is_dnsproxy_enabled_called = true;
    std::move(callback).Run(enabled_);
  }

  bool is_dnsproxy_enabled_called{false};

 private:
  bool enabled_;
};

class FakeSessionMonitor : public SessionMonitor {
 public:
  explicit FakeSessionMonitor(scoped_refptr<dbus::Bus> bus)
      : SessionMonitor(bus) {}

  void Login() { OnSessionStateChanged("started"); }

  void Logout() { OnSessionStateChanged("stopping"); }
};

class MockPatchpanelClient : public patchpanel::Client {
 public:
  MockPatchpanelClient() = default;
  ~MockPatchpanelClient() = default;

  MOCK_METHOD(void,
              RegisterOnAvailableCallback,
              (base::RepeatingCallback<void(bool)>),
              (override));
  MOCK_METHOD(void,
              RegisterProcessChangedCallback,
              (base::RepeatingCallback<void(bool)>),
              (override));
  MOCK_METHOD(bool, NotifyArcStartup, (pid_t), (override));
  MOCK_METHOD(bool, NotifyArcShutdown, (), (override));
  MOCK_METHOD(std::vector<patchpanel::NetworkDevice>,
              NotifyArcVmStartup,
              (uint32_t),
              (override));
  MOCK_METHOD(bool, NotifyArcVmShutdown, (uint32_t), (override));
  MOCK_METHOD(bool,
              NotifyTerminaVmStartup,
              (uint32_t, patchpanel::NetworkDevice*, patchpanel::IPv4Subnet*),
              (override));
  MOCK_METHOD(bool, NotifyTerminaVmShutdown, (uint32_t), (override));
  MOCK_METHOD(bool,
              NotifyPluginVmStartup,
              (uint64_t, int, patchpanel::NetworkDevice*),
              (override));
  MOCK_METHOD(bool, NotifyPluginVmShutdown, (uint64_t), (override));
  MOCK_METHOD(bool, DefaultVpnRouting, (int), (override));
  MOCK_METHOD(bool, RouteOnVpn, (int), (override));
  MOCK_METHOD(bool, BypassVpn, (int), (override));
  MOCK_METHOD((std::pair<base::ScopedFD, patchpanel::ConnectNamespaceResponse>),
              ConnectNamespace,
              (pid_t pid,
               const std::string& outbound_ifname,
               bool forward_user_traffic,
               bool route_on_vpn,
               patchpanel::TrafficCounter::Source traffic_source),
              (override));
  MOCK_METHOD(void,
              GetTrafficCounters,
              (const std::set<std::string>&, GetTrafficCountersCallback),
              (override));
  MOCK_METHOD(bool,
              ModifyPortRule,
              (patchpanel::ModifyPortRuleRequest::Operation,
               patchpanel::ModifyPortRuleRequest::RuleType,
               patchpanel::ModifyPortRuleRequest::Protocol,
               const std::string&,
               const std::string&,
               uint32_t,
               const std::string&,
               uint32_t),
              (override));
  MOCK_METHOD(base::ScopedFD,
              RedirectDns,
              (patchpanel::SetDnsRedirectionRuleRequest::RuleType,
               const std::string&,
               const std::string&,
               const std::vector<std::string>&,
               const std::string&),
              (override));
  MOCK_METHOD(std::vector<patchpanel::NetworkDevice>,
              GetDevices,
              (),
              (override));
  MOCK_METHOD(void,
              RegisterNetworkDeviceChangedSignalHandler,
              (NetworkDeviceChangedSignalHandler),
              (override));
  MOCK_METHOD(void,
              RegisterNeighborReachabilityEventHandler,
              (NeighborReachabilityEventHandler),
              (override));
  MOCK_METHOD(bool, SetVpnLockdown, (bool), (override));
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
  ProxyTest()
      : mock_bus_(new dbus::MockBus{dbus::Bus::Options{}}),
        mock_proxy_(new dbus::MockObjectProxy(mock_bus_.get(),
                                              shill::kFlimflamServiceName,
                                              dbus::ObjectPath("/"))) {}
  ~ProxyTest() { mock_bus_->ShutdownAndBlock(); }

  void SetUp() override {
    EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
        .WillRepeatedly(Return(mock_proxy_.get()));
  }

  std::unique_ptr<FakePatchpanelClient> PatchpanelClient() const {
    return std::make_unique<FakePatchpanelClient>();
  }

  std::unique_ptr<FakeShillClient> ShillClient() const {
    return std::make_unique<FakeShillClient>(
        mock_bus_, reinterpret_cast<ManagerProxyInterface*>(
                       const_cast<ManagerProxyMock*>(&mock_manager_)));
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  ManagerProxyMock mock_manager_;
};

TEST_F(ProxyTest, SystemProxy_OnShutdownClearsAddressPropertyOnShill) {
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _));
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  int unused;
  proxy.shill_ready_ = true;
  proxy.OnShutdown(&unused);
}

TEST_F(ProxyTest, NonSystemProxy_OnShutdownDoesNotCallShill) {
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _)).Times(0);
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _)).Times(0);
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, PatchpanelClient(),
              ShillClient());
  int unused;
  proxy.shill_ready_ = true;
  proxy.OnShutdown(&unused);
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddressesDoesntCrashIfDieFalse) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_CALL(mock_manager_, SetProperty(_, _, _, _)).Times(0);
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.SetShillDNSProxyAddresses("10.10.10.10", "::1", false, 0);
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddresses) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.feature_enabled_ = true;
  proxy.doh_config_.set_nameservers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(mock_manager_,
              SetDNSProxyAddresses(ElementsAre("10.10.10.10", "::1"), _, _))
      .WillOnce(Return(true));
  proxy.SetShillDNSProxyAddresses("10.10.10.10", "::1");
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddressesEmptyNameserver) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.feature_enabled_ = true;

  // Only IPv4 nameserver.
  proxy.doh_config_.set_nameservers({"8.8.8.8"}, {});
  EXPECT_CALL(mock_manager_,
              SetDNSProxyAddresses(ElementsAre("10.10.10.10"), _, _))
      .WillOnce(Return(true));
  proxy.SetShillDNSProxyAddresses("10.10.10.10", "::1");

  // Only IPv6 nameserver.
  proxy.doh_config_.set_nameservers({}, {"2001:4860:4860::8888"});
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(ElementsAre("::1"), _, _))
      .WillOnce(Return(true));
  proxy.SetShillDNSProxyAddresses("10.10.10.10", "::1");
}

TEST_F(ProxyTest, SystemProxy_SetShillDNSProxyAddressesFeatureDisabled) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.feature_enabled_ = false;
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _)).Times(0);
  proxy.SetShillDNSProxyAddresses("10.10.10.10", "::1");
}

TEST_F(ProxyTest, SystemProxy_ClearShillDNSProxyAddresses) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.feature_enabled_ = true;
  proxy.doh_config_.set_nameservers({"8.8.8.8"}, {"2001:4860:4860::8888"});
  EXPECT_CALL(mock_manager_, ClearDNSProxyAddresses(_, _));
  proxy.ClearShillDNSProxyAddresses();
}

TEST_F(ProxyTest, ShillInitializedWhenReady) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.OnShillReady(true);
  EXPECT_TRUE(proxy.shill_ready_);
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

TEST_F(ProxyTest, ShillResetRestoresAddressProperty) {
  auto pp = PatchpanelClient();
  patchpanel::ConnectNamespaceResponse resp;
  resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
  pp->SetConnectNamespaceResult(make_fd(), resp);
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, std::move(pp),
              ShillClient());
  proxy.doh_config_.set_nameservers({"8.8.8.8"}, {"2401::8888"});
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.OnPatchpanelReady(true);
  EXPECT_CALL(mock_manager_,
              SetDNSProxyAddresses(ElementsAre("10.10.10.10", "::1"), _, _))
      .WillOnce(Return(true));
  proxy.shill_ready_ = true;
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
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  dev.ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                     "2001:4860:4860::8844"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy.OnDefaultDeviceChanged(&dev);
}

TEST_F(ProxyTest, SystemProxy_ShillPropertyUpdatedOnDefaultServiceComesOnline) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8"};
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  EXPECT_CALL(*mock_resolver, SetNameServers(_));
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  proxy.OnDefaultDeviceChanged(&dev);
}

TEST_F(ProxyTest, SystemProxy_IgnoresVPN) {
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kSystem},
                  PatchpanelClient(), ShillClient());
  proxy.shill_ready_ = true;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  ON_CALL(*mock_resolver, ListenUDP(_)).WillByDefault(Return(true));
  ON_CALL(*mock_resolver, ListenTCP(_)).WillByDefault(Return(true));
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  proxy.resolver = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.type = shill::Client::Device::Type::kWifi;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8"};
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
  proxy.shill_ready_ = true;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  ON_CALL(*mock_resolver, ListenUDP(_)).WillByDefault(Return(true));
  ON_CALL(*mock_resolver, ListenTCP(_)).WillByDefault(Return(true));
  brillo::VariantDictionary props;
  EXPECT_CALL(mock_manager_, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(props), Return(true)));
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
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
  shill_ptr->default_device_->ipconfig.ipv4_dns_addresses = {"8.8.8.8"};
  proxy.OnDefaultDeviceChanged(&vpn);
  EXPECT_TRUE(proxy.device_);
  EXPECT_EQ(proxy.device_->type, shill::Client::Device::Type::kWifi);
}

TEST_F(ProxyTest, DefaultProxy_UsesVPN) {
  TestProxy proxy(Proxy::Options{.type = Proxy::Type::kDefault},
                  PatchpanelClient(), ShillClient());
  proxy.shill_ready_ = true;
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

TEST_F(ProxyTest, ArcProxy_NameServersUpdatedOnDeviceChangeEvent) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"},
              PatchpanelClient(), ShillClient());
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.ifname = "wlan0";
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  dev.ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                     "2001:4860:4860::8844"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy.OnDeviceChanged(&dev);

  // Verify it only applies changes for the correct interface.
  dev.ifname = "eth0";
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4", "1.1.1.1"};
  EXPECT_CALL(*mock_resolver, SetNameServers(_)).Times(0);
  proxy.OnDeviceChanged(&dev);

  dev.ifname = "wlan0";
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4", "1.1.1.1"};
  dev.ipconfig.ipv6_dns_addresses.clear();
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("1.1.1.1"))));
  proxy.OnDeviceChanged(&dev);
}

TEST_F(ProxyTest, SystemProxy_NameServersUpdatedOnDeviceChangeEvent) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  dev.ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                     "2001:4860:4860::8844"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy.OnDefaultDeviceChanged(&dev);

  // Now trigger an ipconfig change.
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8"};
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
  proxy.OnDeviceChanged(&dev);
}

TEST_F(ProxyTest, DeviceChangeEventIgnored) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.ifname = "eth0";
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  dev.ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                     "2001:4860:4860::8844"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_resolver, ListenUDP(_)).Times(0);
  EXPECT_CALL(*mock_resolver, ListenTCP(_)).Times(0);
  EXPECT_CALL(*mock_resolver,
              SetNameServers(ElementsAre(StrEq("8.8.8.8"), StrEq("8.8.4.4"),
                                         StrEq("2001:4860:4860::8888"),
                                         StrEq("2001:4860:4860::8844"))));
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

TEST_F(ProxyTest, RemovesDNSQueryParameterTemplate_AlwaysOn) {
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
  props["https://dns.google.com{?dns}"] = std::string("");
  proxy.OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, RemovesDNSQueryParameterTemplate_Automatic) {
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
  props["https://dns.google.com{?dns}"] = std::string("8.8.8.8, 8.8.4.4");
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
  ipconfig.ipv4_dns_addresses = {"8.8.8.8"};
  ipconfig.ipv6_dns_addresses = {"2620:119:35::35"};
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

TEST_F(ProxyTest, FeatureEnablementCheckedOnSetup) {
  scoped_refptr<dbus::MockObjectProxy> mock = new dbus::MockObjectProxy(
      mock_bus_.get(), shill::kFlimflamServiceName, dbus::ObjectPath("/"));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
      .WillRepeatedly(Return(mock.get()));

  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.session_.reset(new FakeSessionMonitor(mock_bus_));
  auto features = std::make_unique<FakeFeaturesClient>(true);
  auto* features_ptr = features.get();
  proxy.features_ = std::move(features);
  ASSERT_FALSE(features_ptr->is_dnsproxy_enabled_called);
  proxy.Setup();
  EXPECT_TRUE(features_ptr->is_dnsproxy_enabled_called);
}

TEST_F(ProxyTest, LoginEventTriggersFeatureCheck) {
  scoped_refptr<dbus::MockObjectProxy> mock = new dbus::MockObjectProxy(
      mock_bus_.get(), shill::kFlimflamServiceName, dbus::ObjectPath("/"));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
      .WillRepeatedly(Return(mock.get()));

  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  auto session = std::make_unique<FakeSessionMonitor>(mock_bus_);
  auto* session_ptr = session.get();
  proxy.session_ = std::move(session);
  auto features = std::make_unique<FakeFeaturesClient>(true);
  auto* features_ptr = features.get();
  proxy.features_ = std::move(features);
  proxy.Setup();
  features_ptr->is_dnsproxy_enabled_called = false;
  session_ptr->Login();
  EXPECT_TRUE(features_ptr->is_dnsproxy_enabled_called);
}

TEST_F(ProxyTest, LogoutEventTriggersDisable) {
  scoped_refptr<dbus::MockObjectProxy> mock = new dbus::MockObjectProxy(
      mock_bus_.get(), shill::kFlimflamServiceName, dbus::ObjectPath("/"));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
      .WillRepeatedly(Return(mock.get()));

  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  auto session = std::make_unique<FakeSessionMonitor>(mock_bus_);
  auto* session_ptr = session.get();
  proxy.session_ = std::move(session);
  proxy.features_.reset(new FakeFeaturesClient(true));
  proxy.Setup();
  ASSERT_TRUE(proxy.feature_enabled_);
  session_ptr->Logout();
  EXPECT_FALSE(proxy.feature_enabled_);
}

TEST_F(ProxyTest, FeatureEnabled_LoginAfterLogout) {
  scoped_refptr<dbus::MockObjectProxy> mock = new dbus::MockObjectProxy(
      mock_bus_.get(), shill::kFlimflamServiceName, dbus::ObjectPath("/"));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
      .WillRepeatedly(Return(mock.get()));

  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  auto session = std::make_unique<FakeSessionMonitor>(mock_bus_);
  auto* session_ptr = session.get();
  proxy.session_ = std::move(session);
  proxy.features_.reset(new FakeFeaturesClient(true));
  proxy.Setup();
  session_ptr->Login();
  EXPECT_TRUE(proxy.feature_enabled_);
  session_ptr->Logout();
  EXPECT_FALSE(proxy.feature_enabled_);
  session_ptr->Login();
  EXPECT_TRUE(proxy.feature_enabled_);
}

TEST_F(ProxyTest, FeatureDisabled_LoginAfterLogout) {
  scoped_refptr<dbus::MockObjectProxy> mock = new dbus::MockObjectProxy(
      mock_bus_.get(), shill::kFlimflamServiceName, dbus::ObjectPath("/"));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
      .WillRepeatedly(Return(mock.get()));

  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  auto session = std::make_unique<FakeSessionMonitor>(mock_bus_);
  auto* session_ptr = session.get();
  proxy.session_ = std::move(session);
  proxy.features_.reset(new FakeFeaturesClient(false));
  proxy.Setup();
  session_ptr->Login();
  EXPECT_FALSE(proxy.feature_enabled_);
  session_ptr->Logout();
  EXPECT_FALSE(proxy.feature_enabled_);
  session_ptr->Login();
  EXPECT_FALSE(proxy.feature_enabled_);
}

TEST_F(ProxyTest, SystemProxy_ShillPropertyNotUpdatedIfFeatureDisabled) {
  scoped_refptr<dbus::MockObjectProxy> mock = new dbus::MockObjectProxy(
      mock_bus_.get(), shill::kFlimflamServiceName, dbus::ObjectPath("/"));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(_, _))
      .WillRepeatedly(Return(mock.get()));

  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  proxy.session_.reset(new FakeSessionMonitor(mock_bus_));
  proxy.features_.reset(new FakeFeaturesClient(false));
  proxy.Setup();
  proxy.shill_ready_ = true;

  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  EXPECT_CALL(*mock_resolver, SetNameServers(_));
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _)).Times(0);
  proxy.OnDefaultDeviceChanged(&dev);
}

TEST_F(ProxyTest, DefaultProxy_DisableDoHProvidersOnVPN) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, PatchpanelClient(),
              ShillClient());
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.device_->state = shill::Client::Device::ConnectionState::kOnline;
  proxy.device_->type = shill::Client::Device::Type::kVPN;
  auto resolver = std::make_unique<MockResolver>();
  MockResolver* mock_resolver = resolver.get();
  proxy.resolver_ = std::move(resolver);
  proxy.doh_config_.set_resolver(mock_resolver);
  EXPECT_CALL(*mock_resolver, SetDoHProviders(IsEmpty(), false));
  brillo::VariantDictionary props;
  props["https://dns.google.com"] = std::string("");
  props["https://doh.opendns.com"] = std::string("");
  proxy.OnDoHProvidersChanged(props);
}

TEST_F(ProxyTest, SystemProxy_SetsDnsRedirectionRule) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, std::move(client),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.device_ = std::make_unique<shill::Client::Device>();

  // System proxy requests a DnsRedirectionRule to exclude traffic destined not
  // to the underlying network's name server.
  EXPECT_CALL(
      *mock_client,
      RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION,
                  _, "10.10.10.10", _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  proxy.doh_config_.set_nameservers({"8.8.8.8"}, {});
  proxy.OnPatchpanelReady(true);
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  proxy.Enable();

  // Default device changed.
  shill::Client::Device default_device;
  default_device.ifname = "eth0";
  default_device.state = shill::Client::Device::ConnectionState::kOnline;
  default_device.ipconfig.ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  default_device.ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                                "2001:4860:4860::8844"};
  EXPECT_CALL(
      *mock_client,
      RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::EXCLUDE_DESTINATION,
                  _, "10.10.10.10", _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(mock_manager_, SetDNSProxyAddresses(_, _, _))
      .WillOnce(Return(true));
  proxy.OnDefaultDeviceChanged(&default_device);

  // Plugin VM started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* plugin_vm_dev = signal.mutable_device();
  plugin_vm_dev->set_ifname("vmtap1");
  plugin_vm_dev->set_phys_ifname("eth0");
  plugin_vm_dev->set_guest_type(patchpanel::NetworkDevice::PLUGIN_VM);
  proxy.OnVirtualDeviceChanged(signal);

  // ARC started.
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* arc_dev = signal.mutable_device();
  arc_dev->set_ifname("arc_eth0");
  arc_dev->set_phys_ifname("eth0");
  arc_dev->set_guest_type(patchpanel::NetworkDevice::ARC);
  proxy.OnVirtualDeviceChanged(signal);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleDeviceAlreadyStarted) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.device_ = std::make_unique<shill::Client::Device>();

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);

  EXPECT_CALL(
      *mock_client,
      RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _, _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}))
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
                          "vmtap0", "10.10.10.10", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
                          "vmtap0", "::1", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 4);

  // Default device changed.
  shill::Client::Device default_device;
  default_device.state = shill::Client::Device::ConnectionState::kOnline;
  std::vector<std::string> ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  default_device.ipconfig.ipv4_dns_addresses = ipv4_dns_addresses;
  std::vector<std::string> ipv6_dns_addresses = {"2001:4860:4860::8888",
                                                 "2001:4860:4860::8844"};
  default_device.ipconfig.ipv6_dns_addresses = ipv6_dns_addresses;
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _,
                          ipv4_dns_addresses, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _,
                          ipv6_dns_addresses, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnDefaultDeviceChanged(&default_device);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 4);

  // Guest stopped.
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_REMOVED);
  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleNewDeviceStarted) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });
  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);

  // Default device changed.
  shill::Client::Device default_device;
  default_device.state = shill::Client::Device::ConnectionState::kOnline;
  std::vector<std::string> ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  default_device.ipconfig.ipv4_dns_addresses = ipv4_dns_addresses;
  std::vector<std::string> ipv6_dns_addresses = {"2001:4860:4860::8888",
                                                 "2001:4860:4860::8844"};
  default_device.ipconfig.ipv6_dns_addresses = ipv6_dns_addresses;
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _,
                          ipv4_dns_addresses, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _,
                          ipv6_dns_addresses, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnDefaultDeviceChanged(&default_device);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);

  // Guest started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::PLUGIN_VM);

  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
                          "vmtap0", "10.10.10.10", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
                          "vmtap0", "::1", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 4);

  // Guest stopped.
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_REMOVED);
  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, DefaultProxy_NeverSetsDnsRedirectionRuleOtherGuest) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.device_ = std::make_unique<shill::Client::Device>();

  EXPECT_CALL(
      *mock_client,
      RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _, _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT, _,
                          _, _, _))
      .Times(0);

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);

  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, DefaultProxy_NeverSetsDnsRedirectionRuleFeatureDisabled) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.shill_ready_ = true;
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.feature_enabled_ = false;
  proxy.device_ = std::make_unique<shill::Client::Device>();

  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);

  proxy.OnPatchpanelReady(true);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);

  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleWithoutIPv6) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.device_ = std::make_unique<shill::Client::Device>();

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  EXPECT_CALL(
      *mock_client,
      RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _, _, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 1);

  // Default device changed.
  shill::Client::Device default_device;
  default_device.state = shill::Client::Device::ConnectionState::kOnline;
  std::vector<std::string> ipv4_dns_addresses = {"8.8.8.8", "8.8.4.4"};
  default_device.ipconfig.ipv4_dns_addresses = ipv4_dns_addresses;
  default_device.ipconfig.ipv6_dns_addresses = {"2001:4860:4860::8888",
                                                "2001:4860:4860::8844"};
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _,
                          ipv4_dns_addresses, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnDefaultDeviceChanged(&default_device);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 1);

  // Guest started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::PLUGIN_VM);

  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
                          "vmtap0", "10.10.10.10", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);

  // Guest stopped.
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_REMOVED);
  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 1);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Added) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.ns_.set_peer_ifname("");
  proxy.Enable();
  proxy.device_ = std::make_unique<shill::Client::Device>();

  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);

  std::string peer_ipv6_addr = "::1";
  struct in6_addr ipv6_addr;
  inet_pton(AF_INET6, peer_ipv6_addr.c_str(), &ipv6_addr.s6_addr);

  std::vector<std::string> ipv6_dns_addresses = {"2001:4860:4860::8888",
                                                 "2001:4860:4860::8844"};
  proxy.doh_config_.set_nameservers({"8.8.8.8", "8.8.4.4"}, ipv6_dns_addresses);
  proxy.device_.reset(new shill::Client::Device{});

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::USER, _, _,
                          ipv6_dns_addresses, _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::DEFAULT,
                          "vmtap0", peer_ipv6_addr, IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to 0 in order to match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  shill::RTNLMessage msg(shill::RTNLMessage::kTypeAddress,
                         shill::RTNLMessage::kModeAdd, 0 /* flags */,
                         0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                         shill::IPAddress::Family(AF_INET6));
  msg.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg.SetAttribute(IFA_ADDRESS,
                   shill::ByteString(ipv6_addr.s6_addr, sizeof(ipv6_addr)));
  proxy.RTNLMessageHandler(msg);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Deleted) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.ns_.set_peer_ifname("");
  proxy.device_ = std::make_unique<shill::Client::Device>();

  proxy.device_.reset(new shill::Client::Device{});
  proxy.lifeline_fds_.emplace(std::make_pair("", AF_INET6),
                              base::ScopedFD(make_fd()));
  proxy.lifeline_fds_.emplace(std::make_pair("vmtap0", AF_INET6),
                              base::ScopedFD(make_fd()));

  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));

  shill::RTNLMessage msg(shill::RTNLMessage::kTypeAddress,
                         shill::RTNLMessage::kModeDelete, 0 /* flags */,
                         0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                         shill::IPAddress::Family(AF_INET6));
  msg.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  proxy.RTNLMessageHandler(msg);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, DefaultProxy_SetDnsRedirectionRuleUnrelatedIPv6Added) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kDefault}, std::move(client),
              ShillClient());
  proxy.ns_.set_peer_ifname("");
  proxy.Enable();
  proxy.device_ = std::make_unique<shill::Client::Device>();

  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);

  std::string peer_ipv6_addr = "::1";
  struct in6_addr ipv6_addr;
  inet_pton(AF_INET6, peer_ipv6_addr.c_str(), &ipv6_addr.s6_addr);

  std::vector<std::string> ipv6_dns_addresses = {"2001:4860:4860::8888",
                                                 "2001:4860:4860::8844"};
  proxy.doh_config_.set_nameservers({"8.8.8.8", "8.8.4.4"}, ipv6_dns_addresses);
  proxy.device_.reset(new shill::Client::Device{});

  EXPECT_CALL(*mock_client, GetDevices())
      .WillRepeatedly(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to -1 in order to not match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  shill::RTNLMessage msg_unrelated_ifindex(
      shill::RTNLMessage::kTypeAddress, shill::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      shill::IPAddress::Family(AF_INET6));
  msg_unrelated_ifindex.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg_unrelated_ifindex.SetAttribute(
      IFA_ADDRESS, shill::ByteString(ipv6_addr.s6_addr, sizeof(ipv6_addr)));
  proxy.RTNLMessageHandler(msg_unrelated_ifindex);

  shill::RTNLMessage msg_unrelated_scope(
      shill::RTNLMessage::kTypeAddress, shill::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      shill::IPAddress::Family(AF_INET6));
  msg_unrelated_scope.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_LINK));
  msg_unrelated_scope.SetAttribute(
      IFA_ADDRESS, shill::ByteString(ipv6_addr.s6_addr, sizeof(ipv6_addr)));
  proxy.RTNLMessageHandler(msg_unrelated_scope);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleDeviceAlreadyStarted) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.device_ = std::make_unique<shill::Client::Device>();

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::ARC,
                          "arc_eth0", "10.10.10.10", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::ARC,
                          "arc_eth0", "::1", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleNewDeviceStarted) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.device_ = std::make_unique<shill::Client::Device>();

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });
  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);

  // Guest started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARC);

  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::ARC,
                          "arc_eth0", "10.10.10.10", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::ARC,
                          "arc_eth0", "::1", IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));
  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 2);
}

TEST_F(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherGuest) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.device_ = std::make_unique<shill::Client::Device>();

  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("vmtap0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);

  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherIfname) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "wlan0"},
              std::move(client), ShillClient());
  proxy.shill_ready_ = true;
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.device_ = std::make_unique<shill::Client::Device>();

  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  proxy.OnPatchpanelReady(true);
  proxy.Enable();
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);

  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleFeatureDisabled) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.resolver_ = std::make_unique<MockResolver>();
  proxy.shill_ready_ = true;
  proxy.ns_peer_ipv6_address_ = "::1";
  proxy.feature_enabled_ = false;
  proxy.device_ = std::make_unique<shill::Client::Device>();

  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);

  // Expect ConnectNamespace call and set the namespace address.
  EXPECT_CALL(*mock_client, ConnectNamespace(_, _, _, _, _))
      .WillRepeatedly([](pid_t pid, const std::string& outbound_ifname,
                         bool forward_user_traffic, bool route_on_vpn,
                         patchpanel::TrafficCounter::Source traffic_source) {
        patchpanel::ConnectNamespaceResponse resp;
        resp.set_peer_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
        return std::make_pair(base::ScopedFD(make_fd()), resp);
      });

  // Set devices created before the proxy started.
  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  proxy.OnPatchpanelReady(true);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);

  proxy.OnVirtualDeviceChanged(signal);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Added) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.ns_.set_peer_ifname("");
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.Enable();

  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  std::string peer_ipv6_addr = "::1";
  struct in6_addr ipv6_addr;
  inet_pton(AF_INET6, peer_ipv6_addr.c_str(), &ipv6_addr.s6_addr);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  EXPECT_CALL(*mock_client,
              RedirectDns(patchpanel::SetDnsRedirectionRuleRequest::ARC,
                          "arc_eth0", peer_ipv6_addr, IsEmpty(), _))
      .WillOnce(Return(ByMove(base::ScopedFD(make_fd()))));

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to 0 in order to match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  shill::RTNLMessage msg(shill::RTNLMessage::kTypeAddress,
                         shill::RTNLMessage::kModeAdd, 0 /* flags */,
                         0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                         shill::IPAddress::Family(AF_INET6));
  msg.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg.SetAttribute(IFA_ADDRESS,
                   shill::ByteString(ipv6_addr.s6_addr, sizeof(ipv6_addr)));
  proxy.RTNLMessageHandler(msg);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Deleted) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.ns_.set_peer_ifname("");

  proxy.device_.reset(new shill::Client::Device{});
  proxy.lifeline_fds_.emplace(std::make_pair("arc_eth0", AF_INET6),
                              base::ScopedFD(make_fd()));

  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillOnce(Return(std::vector<patchpanel::NetworkDevice>{*dev}));

  shill::RTNLMessage msg(shill::RTNLMessage::kTypeAddress,
                         shill::RTNLMessage::kModeDelete, 0 /* flags */,
                         0 /* seq */, 0 /* pid */, 0 /* interface_index */,
                         shill::IPAddress::Family(AF_INET6));
  msg.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  proxy.RTNLMessageHandler(msg);
  EXPECT_EQ(proxy.lifeline_fds_.size(), 0);
}

TEST_F(ProxyTest, ArcProxy_SetDnsRedirectionRuleUnrelatedIPv6Added) {
  auto client = std::make_unique<MockPatchpanelClient>();
  MockPatchpanelClient* mock_client = client.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kARC, .ifname = "eth0"},
              std::move(client), ShillClient());
  proxy.ns_.set_peer_ifname("");
  proxy.device_ = std::make_unique<shill::Client::Device>();
  proxy.Enable();

  patchpanel::NetworkDeviceChangedSignal signal;
  signal.set_event(patchpanel::NetworkDeviceChangedSignal::DEVICE_ADDED);
  auto* dev = signal.mutable_device();
  dev->set_ifname("arc_eth0");
  dev->set_phys_ifname("eth0");
  dev->set_guest_type(patchpanel::NetworkDevice::ARCVM);

  std::string peer_ipv6_addr = "::1";
  struct in6_addr ipv6_addr;
  inet_pton(AF_INET6, peer_ipv6_addr.c_str(), &ipv6_addr.s6_addr);

  EXPECT_CALL(*mock_client, GetDevices())
      .WillRepeatedly(Return(std::vector<patchpanel::NetworkDevice>{*dev}));
  EXPECT_CALL(*mock_client, RedirectDns(_, _, _, _, _)).Times(0);

  // Proxy's ConnectedNamespace peer interface name is set to empty and
  // RTNL message's interface index is set to -1 in order to not match.
  // if_nametoindex which is used to get the interface index will return 0 on
  // error.
  shill::RTNLMessage msg_unrelated_ifindex(
      shill::RTNLMessage::kTypeAddress, shill::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      shill::IPAddress::Family(AF_INET6));
  msg_unrelated_ifindex.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_UNIVERSE));
  msg_unrelated_ifindex.SetAttribute(
      IFA_ADDRESS, shill::ByteString(ipv6_addr.s6_addr, sizeof(ipv6_addr)));
  proxy.RTNLMessageHandler(msg_unrelated_ifindex);

  shill::RTNLMessage msg_unrelated_scope(
      shill::RTNLMessage::kTypeAddress, shill::RTNLMessage::kModeAdd,
      0 /* flags */, 0 /* seq */, 0 /* pid */, -1 /* interface_index */,
      shill::IPAddress::Family(AF_INET6));
  msg_unrelated_scope.set_address_status(
      shill::RTNLMessage::AddressStatus(0, 0, RT_SCOPE_LINK));
  msg_unrelated_scope.SetAttribute(
      IFA_ADDRESS, shill::ByteString(ipv6_addr.s6_addr, sizeof(ipv6_addr)));
  proxy.RTNLMessageHandler(msg_unrelated_scope);
}

TEST_F(ProxyTest, UpdateNameServers) {
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              ShillClient());
  shill::Client::IPConfig ipconfig;
  ipconfig.ipv4_dns_addresses = {"8.8.4.4",
                                 "192.168.1.1",
                                 "256.256.256.256",
                                 "0.0.0.0",
                                 "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16d",
                                 "::2",
                                 "::",
                                 "a",
                                 ""};
  ipconfig.ipv6_dns_addresses = {"8.8.4.4",
                                 "192.168.1.1",
                                 "256.256.256.256",
                                 "0.0.0.0",
                                 "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16e",
                                 "::1",
                                 "::",
                                 "a",
                                 ""};
  proxy.UpdateNameServers(ipconfig);
  const std::string expected_ipv4_dns_addresses[] = {"8.8.4.4", "192.168.1.1"};
  const std::string expected_ipv6_dns_addresses[] = {
      "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16d", "::2",
      "eeb0:117e:92ee:ad3d:ce0d:a646:95ea:a16e", "::1"};
  EXPECT_THAT(proxy.doh_config_.ipv4_nameservers(),
              ElementsAreArray(expected_ipv4_dns_addresses));
  EXPECT_THAT(proxy.doh_config_.ipv6_nameservers(),
              ElementsAreArray(expected_ipv6_dns_addresses));
}
}  // namespace dns_proxy

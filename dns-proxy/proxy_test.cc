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
}  // namespace
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ManagerProxyMock;
using testing::_;
using testing::Return;
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

  bool IsInitialized() const { return init_; }

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
  MockResolver() : Resolver(kRequestTimeout) {}
  ~MockResolver() = default;

  MOCK_METHOD(bool, Listen, (struct sockaddr*), (override));
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
  std::unique_ptr<Resolver> NewResolver(
      base::TimeDelta request_timeout) override {
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

TEST_F(ProxyTest, SetupInitializesShill) {
  auto shill = ShillClient();
  auto* shill_ptr = shill.get();
  Proxy proxy(Proxy::Options{.type = Proxy::Type::kSystem}, PatchpanelClient(),
              std::move(shill));
  proxy.Setup();
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
  resp.set_host_ipv4_address(patchpanel::Ipv4Addr(10, 10, 10, 10));
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
  EXPECT_CALL(*mock_resolver, Listen(_)).WillOnce(Return(true));
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
  ON_CALL(*mock_resolver, Listen(_)).WillByDefault(Return(false));
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
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  dev.ipconfig.ipv4_dns_addresses = {"a", "b"};
  dev.ipconfig.ipv6_dns_addresses = {"c", "d"};
  // Doesn't call listen since the resolver already exists.
  EXPECT_CALL(*mock_resolver, Listen(_)).Times(0);
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
  shill::Client::Device dev;
  dev.state = shill::Client::Device::ConnectionState::kOnline;
  EXPECT_CALL(*mock_resolver, SetNameServers(_));
  EXPECT_CALL(mock_manager_,
              SetProperty(shill::kDNSProxyIPv4AddressProperty, _, _, _))
      .WillOnce(Return(true));
  proxy.OnDefaultDeviceChanged(&dev);
}

}  // namespace dns_proxy

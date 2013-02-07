// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/openvpn_driver.h"

#include <algorithm>

#include <base/file_path.h>
#include <base/file_util.h>
#include <base/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/dbus_adaptor.h"
#include "shill/error.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_device_info.h"
#include "shill/mock_glib.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_nss.h"
#include "shill/mock_openvpn_management_server.h"
#include "shill/mock_process_killer.h"
#include "shill/mock_service.h"
#include "shill/mock_store.h"
#include "shill/mock_vpn.h"
#include "shill/mock_vpn_service.h"
#include "shill/nice_mock_control.h"
#include "shill/rpc_task.h"
#include "shill/vpn.h"
#include "shill/vpn_service.h"

using base::WeakPtr;
using std::map;
using std::string;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::Field;
using testing::Ne;
using testing::NiceMock;
using testing::Return;
using testing::SetArgumentPointee;
using testing::StrictMock;

namespace shill {

class OpenVPNDriverTest : public testing::Test,
                          public RPCTaskDelegate {
 public:
  OpenVPNDriverTest()
      : device_info_(&control_, &dispatcher_, &metrics_, &manager_),
        metrics_(&dispatcher_),
        manager_(&control_, &dispatcher_, &metrics_, &glib_),
        driver_(new OpenVPNDriver(&control_, &dispatcher_, &metrics_, &manager_,
                                  &device_info_, &glib_)),
        service_(new MockVPNService(&control_, &dispatcher_, &metrics_,
                                    &manager_, driver_)),
        device_(new MockVPN(&control_, &dispatcher_, &metrics_, &manager_,
                            kInterfaceName, kInterfaceIndex)),
        management_server_(new NiceMock<MockOpenVPNManagementServer>()) {
    driver_->management_server_.reset(management_server_);
    driver_->nss_ = &nss_;
    driver_->process_killer_ = &process_killer_;
  }

  virtual ~OpenVPNDriverTest() {}

  virtual void TearDown() {
    driver_->default_service_callback_tag_ = 0;
    driver_->child_watch_tag_ = 0;
    driver_->pid_ = 0;
    driver_->device_ = NULL;
    driver_->service_ = NULL;
    if (!lsb_release_file_.empty()) {
      EXPECT_TRUE(file_util::Delete(lsb_release_file_, false));
      lsb_release_file_.clear();
    }
  }

 protected:
  static const char kOption[];
  static const char kProperty[];
  static const char kValue[];
  static const char kOption2[];
  static const char kProperty2[];
  static const char kValue2[];
  static const char kGateway1[];
  static const char kNetmask1[];
  static const char kNetwork1[];
  static const char kGateway2[];
  static const char kNetmask2[];
  static const char kNetwork2[];
  static const char kInterfaceName[];
  static const int kInterfaceIndex;

  void SetArg(const string &arg, const string &value) {
    driver_->args()->SetString(arg, value);
  }

  KeyValueStore *GetArgs() {
    return driver_->args();
  }

  void RemoveStringArg(const string &arg) {
    driver_->args()->RemoveString(arg);
  }

  const ServiceRefPtr &GetSelectedService() {
    return device_->selected_service();
  }

  bool InitManagementChannelOptions(vector<string> *options, Error *error) {
    return driver_->InitManagementChannelOptions(options, error);
  }

  Sockets *GetSockets() {
    return &driver_->sockets_;
  }

  void SetDevice(const VPNRefPtr &device) {
    driver_->device_ = device;
  }

  void SetService(const VPNServiceRefPtr &service) {
    driver_->service_ = service;
  }

  VPNServiceRefPtr GetService() {
    return driver_->service_;
  }

  void OnConnectionDisconnected() {
    driver_->OnConnectionDisconnected();
  }

  void OnConnectTimeout() {
    driver_->OnConnectTimeout();
  }

  void StartConnectTimeout() {
    driver_->StartConnectTimeout();
  }

  bool IsConnectTimeoutStarted() {
    return driver_->IsConnectTimeoutStarted();
  }

  // Used to assert that a flag appears in the options.
  void ExpectInFlags(const vector<string> &options, const string &flag,
                     const string &value);
  void ExpectInFlags(const vector<string> &options, const string &flag);
  void ExpectNotInFlags(const vector<string> &options, const string &flag);

  void SetupLSBRelease();

  // Inherited from RPCTaskDelegate.
  virtual void GetLogin(string *user, string *password);
  virtual void Notify(const string &reason, const map<string, string> &dict);

  NiceMockControl control_;
  NiceMock<MockDeviceInfo> device_info_;
  EventDispatcher dispatcher_;
  MockMetrics metrics_;
  MockGLib glib_;
  MockManager manager_;
  OpenVPNDriver *driver_;  // Owned by |service_|.
  scoped_refptr<MockVPNService> service_;
  scoped_refptr<MockVPN> device_;
  MockNSS nss_;
  MockProcessKiller process_killer_;

  // Owned by |driver_|.
  NiceMock<MockOpenVPNManagementServer> *management_server_;

  FilePath lsb_release_file_;
};

const char OpenVPNDriverTest::kOption[] = "--openvpn-option";
const char OpenVPNDriverTest::kProperty[] = "OpenVPN.SomeProperty";
const char OpenVPNDriverTest::kValue[] = "some-property-value";
const char OpenVPNDriverTest::kOption2[] = "--openvpn-option2";
const char OpenVPNDriverTest::kProperty2[] = "OpenVPN.SomeProperty2";
const char OpenVPNDriverTest::kValue2[] = "some-property-value2";
const char OpenVPNDriverTest::kGateway1[] = "10.242.2.13";
const char OpenVPNDriverTest::kNetmask1[] = "255.255.255.255";
const char OpenVPNDriverTest::kNetwork1[] = "10.242.2.1";
const char OpenVPNDriverTest::kGateway2[] = "10.242.2.14";
const char OpenVPNDriverTest::kNetmask2[] = "255.255.0.0";
const char OpenVPNDriverTest::kNetwork2[] = "192.168.0.0";
const char OpenVPNDriverTest::kInterfaceName[] = "tun0";
const int OpenVPNDriverTest::kInterfaceIndex = 123;

void OpenVPNDriverTest::GetLogin(string */*user*/, string */*password*/) {}

void OpenVPNDriverTest::Notify(const string &/*reason*/,
                               const map<string, string> &/*dict*/) {}

void OpenVPNDriverTest::ExpectInFlags(const vector<string> &options,
                                      const string &flag,
                                      const string &value) {
  vector<string>::const_iterator it =
      std::find(options.begin(), options.end(), flag);

  EXPECT_TRUE(it != options.end());
  if (it != options.end())
    return;  // Don't crash below.
  it++;
  EXPECT_TRUE(it != options.end());
  if (it != options.end())
    return;  // Don't crash below.
  EXPECT_EQ(value, *it);
}

void OpenVPNDriverTest::ExpectInFlags(const vector<string> &options,
                                      const string &flag) {
  EXPECT_TRUE(std::find(options.begin(), options.end(), flag) !=
              options.end());
}

void OpenVPNDriverTest::ExpectNotInFlags(const vector<string> &options,
                                         const string &flag) {
  EXPECT_TRUE(std::find(options.begin(), options.end(), flag) ==
              options.end());
}

void OpenVPNDriverTest::SetupLSBRelease() {
  static const char kLSBReleaseContents[] =
      "\n"
      "=\n"
      "foo=\n"
      "=bar\n"
      "zoo==\n"
      "CHROMEOS_RELEASE_BOARD=x86-alex\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_VERSION=2202.0\n";
  EXPECT_TRUE(file_util::CreateTemporaryFile(&lsb_release_file_));
  EXPECT_EQ(arraysize(kLSBReleaseContents),
            file_util::WriteFile(lsb_release_file_,
                                 kLSBReleaseContents,
                                 arraysize(kLSBReleaseContents)));
  EXPECT_EQ(OpenVPNDriver::kLSBReleaseFile, driver_->lsb_release_file_.value());
  driver_->lsb_release_file_ = lsb_release_file_;
}

TEST_F(OpenVPNDriverTest, Connect) {
  EXPECT_CALL(*service_, SetState(Service::kStateConfiguring));
  const string interface = kInterfaceName;
  EXPECT_CALL(device_info_, CreateTunnelInterface(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(interface), Return(true)));
  Error error;
  driver_->Connect(service_, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kInterfaceName, driver_->tunnel_interface_);
  EXPECT_TRUE(driver_->IsConnectTimeoutStarted());
}

TEST_F(OpenVPNDriverTest, ConnectTunnelFailure) {
  EXPECT_CALL(*service_, SetState(Service::kStateConfiguring));
  EXPECT_CALL(device_info_, CreateTunnelInterface(_)).WillOnce(Return(false));
  EXPECT_CALL(*service_, SetState(Service::kStateFailure));
  Error error;
  driver_->Connect(service_, &error);
  EXPECT_EQ(Error::kInternalError, error.type());
  EXPECT_TRUE(driver_->tunnel_interface_.empty());
  EXPECT_FALSE(driver_->IsConnectTimeoutStarted());
}

namespace {
MATCHER_P(IsIPAddress, address, "") {
  IPAddress ip_address(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(ip_address.SetAddressFromString(address));
  return ip_address.Equals(arg);
}
}  // namespace

TEST_F(OpenVPNDriverTest, Notify) {
  map<string, string> config;
  driver_->service_ = service_;
  driver_->device_ = device_;
  driver_->StartConnectTimeout();
  EXPECT_CALL(*device_,
              UpdateIPConfig(Field(&IPConfig::Properties::address, "")));
  driver_->Notify("up", config);
  EXPECT_FALSE(driver_->IsConnectTimeoutStarted());
  EXPECT_TRUE(GetSelectedService().get() == service_.get());

  // Tests that existing properties are reused if no new ones provided.
  driver_->ip_properties_.address = "1.2.3.4";
  EXPECT_CALL(*device_,
              UpdateIPConfig(Field(&IPConfig::Properties::address, "1.2.3.4")));
  driver_->Notify("up", config);
}

TEST_F(OpenVPNDriverTest, NotifyFail) {
  map<string, string> dict;
  driver_->device_ = device_;
  driver_->StartConnectTimeout();
  EXPECT_CALL(*device_, OnDisconnected());
  driver_->Notify("fail", dict);
  EXPECT_TRUE(driver_->IsConnectTimeoutStarted());
}

TEST_F(OpenVPNDriverTest, GetRouteOptionEntry) {
  OpenVPNDriver::RouteOptions routes;
  EXPECT_EQ(NULL, OpenVPNDriver::GetRouteOptionEntry("foo", "bar", &routes));
  EXPECT_TRUE(routes.empty());
  EXPECT_EQ(NULL, OpenVPNDriver::GetRouteOptionEntry("foo", "foo", &routes));
  EXPECT_TRUE(routes.empty());
  EXPECT_EQ(NULL, OpenVPNDriver::GetRouteOptionEntry("foo", "fooZ", &routes));
  EXPECT_TRUE(routes.empty());
  IPConfig::Route *route =
      OpenVPNDriver::GetRouteOptionEntry("foo", "foo12", &routes);
  EXPECT_EQ(1, routes.size());
  EXPECT_EQ(route, &routes[12]);
  route = OpenVPNDriver::GetRouteOptionEntry("foo", "foo13", &routes);
  EXPECT_EQ(2, routes.size());
  EXPECT_EQ(route, &routes[13]);
}

TEST_F(OpenVPNDriverTest, ParseRouteOption) {
  OpenVPNDriver::RouteOptions routes;
  OpenVPNDriver::ParseRouteOption("foo", "bar", &routes);
  EXPECT_TRUE(routes.empty());
  OpenVPNDriver::ParseRouteOption("gateway_2", kGateway2, &routes);
  OpenVPNDriver::ParseRouteOption("netmask_2", kNetmask2, &routes);
  OpenVPNDriver::ParseRouteOption("network_2", kNetwork2, &routes);
  EXPECT_EQ(1, routes.size());
  OpenVPNDriver::ParseRouteOption("gateway_1", kGateway1, &routes);
  OpenVPNDriver::ParseRouteOption("netmask_1", kNetmask1, &routes);
  OpenVPNDriver::ParseRouteOption("network_1", kNetwork1, &routes);
  EXPECT_EQ(2, routes.size());
  EXPECT_EQ(kGateway1, routes[1].gateway);
  EXPECT_EQ(kNetmask1, routes[1].netmask);
  EXPECT_EQ(kNetwork1, routes[1].host);
  EXPECT_EQ(kGateway2, routes[2].gateway);
  EXPECT_EQ(kNetmask2, routes[2].netmask);
  EXPECT_EQ(kNetwork2, routes[2].host);
}

TEST_F(OpenVPNDriverTest, SetRoutes) {
  OpenVPNDriver::RouteOptions routes;
  routes[1].gateway = "1.2.3.4";
  routes[1].host= "1.2.3.4";
  routes[2].host = "2.3.4.5";
  routes[2].netmask = "255.0.0.0";
  routes[3].netmask = "255.0.0.0";
  routes[3].gateway = "1.2.3.5";
  routes[5].host = kNetwork2;
  routes[5].netmask = kNetmask2;
  routes[5].gateway = kGateway2;
  routes[4].host = kNetwork1;
  routes[4].netmask = kNetmask1;
  routes[4].gateway = kGateway1;
  IPConfig::Properties props;
  OpenVPNDriver::SetRoutes(routes, &props);
  ASSERT_EQ(2, props.routes.size());
  EXPECT_EQ(kGateway1, props.routes[0].gateway);
  EXPECT_EQ(kNetmask1, props.routes[0].netmask);
  EXPECT_EQ(kNetwork1, props.routes[0].host);
  EXPECT_EQ(kGateway2, props.routes[1].gateway);
  EXPECT_EQ(kNetmask2, props.routes[1].netmask);
  EXPECT_EQ(kNetwork2, props.routes[1].host);

  // Tests that the routes are not reset if no new routes are supplied.
  OpenVPNDriver::SetRoutes(OpenVPNDriver::RouteOptions(), &props);
  EXPECT_EQ(2, props.routes.size());
}

TEST_F(OpenVPNDriverTest, SplitPortFromHost) {
  string name, port;
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("", NULL, NULL));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com:", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost(":1234", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com:f:1234", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com:x", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com:-1", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com:+1", &name, &port));
  EXPECT_FALSE(OpenVPNDriver::SplitPortFromHost("v.com:65536", &name, &port));
  EXPECT_TRUE(OpenVPNDriver::SplitPortFromHost("v.com:0", &name, &port));
  EXPECT_EQ("v.com", name);
  EXPECT_EQ("0", port);
  EXPECT_TRUE(OpenVPNDriver::SplitPortFromHost("w.com:65535", &name, &port));
  EXPECT_EQ("w.com", name);
  EXPECT_EQ("65535", port);
  EXPECT_TRUE(OpenVPNDriver::SplitPortFromHost("x.com:12345", &name, &port));
  EXPECT_EQ("x.com", name);
  EXPECT_EQ("12345", port);
}

TEST_F(OpenVPNDriverTest, ParseForeignOption) {
  vector<string> domain_search;
  vector<string> dns_servers;
  IPConfig::Properties props;
  OpenVPNDriver::ParseForeignOption("", &domain_search, &dns_servers);
  OpenVPNDriver::ParseForeignOption(
      "dhcp-option DOMAIN", &domain_search, &dns_servers);
  OpenVPNDriver::ParseForeignOption(
      "dhcp-option DOMAIN zzz.com foo", &domain_search, &dns_servers);
  OpenVPNDriver::ParseForeignOption(
      "dhcp-Option DOmAIN xyz.com", &domain_search, &dns_servers);
  ASSERT_EQ(1, domain_search.size());
  EXPECT_EQ("xyz.com", domain_search[0]);
  OpenVPNDriver::ParseForeignOption(
      "dhcp-option DnS 1.2.3.4", &domain_search, &dns_servers);
  ASSERT_EQ(1, dns_servers.size());
  EXPECT_EQ("1.2.3.4", dns_servers[0]);
}

TEST_F(OpenVPNDriverTest, ParseForeignOptions) {
  // This also tests that std::map is a sorted container.
  map<int, string> options;
  options[5] = "dhcp-option DOMAIN five.com";
  options[2] = "dhcp-option DOMAIN two.com";
  options[8] = "dhcp-option DOMAIN eight.com";
  options[7] = "dhcp-option DOMAIN seven.com";
  options[4] = "dhcp-option DOMAIN four.com";
  options[10] = "dhcp-option dns 1.2.3.4";
  IPConfig::Properties props;
  OpenVPNDriver::ParseForeignOptions(options, &props);
  ASSERT_EQ(5, props.domain_search.size());
  EXPECT_EQ("two.com", props.domain_search[0]);
  EXPECT_EQ("four.com", props.domain_search[1]);
  EXPECT_EQ("five.com", props.domain_search[2]);
  EXPECT_EQ("seven.com", props.domain_search[3]);
  EXPECT_EQ("eight.com", props.domain_search[4]);
  ASSERT_EQ(1, props.dns_servers.size());
  EXPECT_EQ("1.2.3.4", props.dns_servers[0]);

  // Test that the DNS properties are not updated if no new DNS properties are
  // supplied.
  OpenVPNDriver::ParseForeignOptions(map<int, string>(), &props);
  EXPECT_EQ(5, props.domain_search.size());
  ASSERT_EQ(1, props.dns_servers.size());
}

TEST_F(OpenVPNDriverTest, ParseIPConfiguration) {
  map<string, string> config;
  IPConfig::Properties props;

  OpenVPNDriver::ParseIPConfiguration(config, &props);
  EXPECT_EQ(IPAddress::kFamilyIPv4, props.address_family);
  EXPECT_EQ(32, props.subnet_prefix);

  props.subnet_prefix = 18;
  OpenVPNDriver::ParseIPConfiguration(config, &props);
  EXPECT_EQ(18, props.subnet_prefix);

  config["ifconfig_loCal"] = "4.5.6.7";
  config["ifconfiG_broadcast"] = "1.2.255.255";
  config["ifconFig_netmAsk"] = "255.255.255.0";
  config["ifconfig_remotE"] = "33.44.55.66";
  config["route_vpN_gateway"] = "192.168.1.1";
  config["trusted_ip"] = "99.88.77.66";
  config["tun_mtu"] = "1000";
  config["foreign_option_2"] = "dhcp-option DNS 4.4.4.4";
  config["foreign_option_1"] = "dhcp-option DNS 1.1.1.1";
  config["foreign_option_3"] = "dhcp-option DNS 2.2.2.2";
  config["route_network_2"] = kNetwork2;
  config["route_network_1"] = kNetwork1;
  config["route_netmask_2"] = kNetmask2;
  config["route_netmask_1"] = kNetmask1;
  config["route_gateway_2"] = kGateway2;
  config["route_gateway_1"] = kGateway1;
  config["foo"] = "bar";
  OpenVPNDriver::ParseIPConfiguration(config, &props);
  EXPECT_EQ(IPAddress::kFamilyIPv4, props.address_family);
  EXPECT_EQ("4.5.6.7", props.address);
  EXPECT_EQ("1.2.255.255", props.broadcast_address);
  EXPECT_EQ(24, props.subnet_prefix);
  EXPECT_EQ("33.44.55.66", props.peer_address);
  EXPECT_EQ("192.168.1.1", props.gateway);
  EXPECT_EQ("99.88.77.66", props.trusted_ip);
  EXPECT_EQ(1000, props.mtu);
  ASSERT_EQ(3, props.dns_servers.size());
  EXPECT_EQ("1.1.1.1", props.dns_servers[0]);
  EXPECT_EQ("4.4.4.4", props.dns_servers[1]);
  EXPECT_EQ("2.2.2.2", props.dns_servers[2]);
  ASSERT_EQ(2, props.routes.size());
  EXPECT_EQ(kGateway1, props.routes[0].gateway);
  EXPECT_EQ(kNetmask1, props.routes[0].netmask);
  EXPECT_EQ(kNetwork1, props.routes[0].host);
  EXPECT_EQ(kGateway2, props.routes[1].gateway);
  EXPECT_EQ(kNetmask2, props.routes[1].netmask);
  EXPECT_EQ(kNetwork2, props.routes[1].host);
  EXPECT_FALSE(props.blackhole_ipv6);
}

TEST_F(OpenVPNDriverTest, InitOptionsNoHost) {
  Error error;
  vector<string> options;
  driver_->InitOptions(&options, &error);
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_TRUE(options.empty());
}

TEST_F(OpenVPNDriverTest, InitOptions) {
  static const char kHost[] = "192.168.2.254";
  static const char kTLSAuthContents[] = "SOME-RANDOM-CONTENTS\n";
  static const char kID[] = "TestPKCS11ID";
  FilePath empty_cert;
  SetArg(flimflam::kProviderHostProperty, kHost);
  SetArg(flimflam::kOpenVPNTLSAuthContentsProperty, kTLSAuthContents);
  SetArg(flimflam::kOpenVPNClientCertIdProperty, kID);
  driver_->rpc_task_.reset(new RPCTask(&control_, this));
  driver_->tunnel_interface_ = kInterfaceName;
  EXPECT_CALL(*management_server_, Start(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsOnline()).WillOnce(Return(false));

  Error error;
  vector<string> options;
  driver_->InitOptions(&options, &error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ("--client", options[0]);
  ExpectInFlags(options, "--remote", kHost);
  ExpectInFlags(options, kRPCTaskPathVariable, RPCTaskMockAdaptor::kRpcId);
  ExpectInFlags(options, "--dev", kInterfaceName);
  ExpectInFlags(options, "--group", "openvpn");
  EXPECT_EQ(kInterfaceName, driver_->tunnel_interface_);
  ASSERT_FALSE(driver_->tls_auth_file_.empty());
  ExpectInFlags(options, "--tls-auth", driver_->tls_auth_file_.value());
  string contents;
  EXPECT_TRUE(
      file_util::ReadFileToString(driver_->tls_auth_file_, &contents));
  EXPECT_EQ(kTLSAuthContents, contents);
  ExpectInFlags(options, "--pkcs11-id", kID);
  ExpectInFlags(options, "--ca", OpenVPNDriver::kDefaultCACertificates);
  ExpectInFlags(options, "--syslog");
  ExpectInFlags(options, "--auth-user-pass");
}

TEST_F(OpenVPNDriverTest, InitOptionsHostWithPort) {
  SetArg(flimflam::kProviderHostProperty, "v.com:1234");
  driver_->rpc_task_.reset(new RPCTask(&control_, this));
  driver_->tunnel_interface_ = kInterfaceName;
  EXPECT_CALL(*management_server_, Start(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsOnline()).WillOnce(Return(false));

  Error error;
  vector<string> options;
  driver_->InitOptions(&options, &error);
  EXPECT_TRUE(error.IsSuccess());
  vector<string>::const_iterator it =
      std::find(options.begin(), options.end(), "--remote");
  ASSERT_TRUE(it != options.end());
  ASSERT_TRUE(++it != options.end());
  EXPECT_EQ("v.com", *it);
  ASSERT_TRUE(++it != options.end());
  EXPECT_EQ("1234", *it);
}

TEST_F(OpenVPNDriverTest, InitCAOptions) {
  static const char kHost[] = "192.168.2.254";
  static const char kCaCert[] = "foo";
  static const char kCaCertNSS[] = "{1234}";
  static const char kNSSCertfile[] = "/tmp/nss-cert";

  Error error;
  vector<string> options;
  EXPECT_TRUE(driver_->InitCAOptions(&options, &error));
  EXPECT_TRUE(error.IsSuccess());
  ExpectInFlags(options, "--ca", OpenVPNDriver::kDefaultCACertificates);

  options.clear();
  SetArg(flimflam::kOpenVPNCaCertProperty, kCaCert);
  EXPECT_TRUE(driver_->InitCAOptions(&options, &error));
  ExpectInFlags(options, "--ca", kCaCert);
  EXPECT_TRUE(error.IsSuccess());

  SetArg(flimflam::kOpenVPNCaCertNSSProperty, kCaCertNSS);
  EXPECT_FALSE(driver_->InitCAOptions(&options, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("Can't specify both CACert and CACertNSS.", error.message());

  SetArg(flimflam::kOpenVPNCaCertProperty, "");
  SetArg(flimflam::kProviderHostProperty, kHost);
  FilePath empty_cert;
  FilePath nss_cert(kNSSCertfile);
  EXPECT_CALL(nss_,
              GetPEMCertfile(kCaCertNSS,
                             ElementsAreArray(kHost, arraysize(kHost) - 1)))
      .WillOnce(Return(empty_cert))
      .WillOnce(Return(nss_cert));

  error.Reset();
  EXPECT_FALSE(driver_->InitCAOptions(&options, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("Unable to extract NSS CA certificate: {1234}", error.message());

  error.Reset();
  options.clear();
  EXPECT_TRUE(driver_->InitCAOptions(&options, &error));
  ExpectInFlags(options, "--ca", kNSSCertfile);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(OpenVPNDriverTest, InitClientAuthOptions) {
  static const char kTestValue[] = "foo";
  vector<string> options;

  // No key or cert, assume user/password authentication.
  driver_->InitClientAuthOptions(&options);
  ExpectInFlags(options, "--auth-user-pass");
  ExpectNotInFlags(options, "--key");
  ExpectNotInFlags(options, "--cert");

  // Cert available, no user/password.
  options.clear();
  SetArg(OpenVPNDriver::kOpenVPNCertProperty, kTestValue);
  driver_->InitClientAuthOptions(&options);
  ExpectNotInFlags(options, "--auth-user-pass");
  ExpectNotInFlags(options, "--key");
  ExpectInFlags(options, "--cert", kTestValue);

  // Key available, no user/password.
  options.clear();
  SetArg(OpenVPNDriver::kOpenVPNKeyProperty, kTestValue);
  driver_->InitClientAuthOptions(&options);
  ExpectNotInFlags(options, "--auth-user-pass");
  ExpectInFlags(options, "--key", kTestValue);

  // Key available, AuthUserPass set.
  options.clear();
  SetArg(flimflam::kOpenVPNAuthUserPassProperty, kTestValue);
  driver_->InitClientAuthOptions(&options);
  ExpectInFlags(options, "--auth-user-pass");
  ExpectInFlags(options, "--key", kTestValue);

  // Key available, User set.
  options.clear();
  RemoveStringArg(flimflam::kOpenVPNAuthUserPassProperty);
  SetArg(flimflam::kOpenVPNUserProperty, "user");
  driver_->InitClientAuthOptions(&options);
  ExpectInFlags(options, "--auth-user-pass");
  ExpectInFlags(options, "--key", kTestValue);
}

TEST_F(OpenVPNDriverTest, InitPKCS11Options) {
  vector<string> options;
  driver_->InitPKCS11Options(&options);
  EXPECT_TRUE(options.empty());

  static const char kID[] = "TestPKCS11ID";
  SetArg(flimflam::kOpenVPNClientCertIdProperty, kID);
  driver_->InitPKCS11Options(&options);
  ExpectInFlags(options, "--pkcs11-id", kID);
  ExpectInFlags(options, "--pkcs11-providers", "libchaps.so");

  static const char kProvider[] = "libpkcs11.so";
  SetArg(flimflam::kOpenVPNProviderProperty, kProvider);
  options.clear();
  driver_->InitPKCS11Options(&options);
  ExpectInFlags(options, "--pkcs11-id", kID);
  ExpectInFlags(options, "--pkcs11-providers", kProvider);
}

TEST_F(OpenVPNDriverTest, InitManagementChannelOptionsServerFail) {
  vector<string> options;
  EXPECT_CALL(*management_server_, Start(&dispatcher_, GetSockets(), &options))
      .WillOnce(Return(false));
  Error error;
  EXPECT_FALSE(InitManagementChannelOptions(&options, &error));
  EXPECT_EQ(Error::kInternalError, error.type());
  EXPECT_EQ("Unable to setup management channel.", error.message());
}

TEST_F(OpenVPNDriverTest, InitManagementChannelOptionsOnline) {
  vector<string> options;
  EXPECT_CALL(*management_server_, Start(&dispatcher_, GetSockets(), &options))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, IsOnline()).WillOnce(Return(true));
  EXPECT_CALL(*management_server_, ReleaseHold());
  Error error;
  EXPECT_TRUE(InitManagementChannelOptions(&options, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(OpenVPNDriverTest, InitManagementChannelOptionsOffline) {
  vector<string> options;
  EXPECT_CALL(*management_server_, Start(&dispatcher_, GetSockets(), &options))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, IsOnline()).WillOnce(Return(false));
  EXPECT_CALL(*management_server_, ReleaseHold()).Times(0);
  Error error;
  EXPECT_TRUE(InitManagementChannelOptions(&options, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(OpenVPNDriverTest, InitLoggingOptions) {
  vector<string> options;
  bool vpn_logging = SLOG_IS_ON(VPN, 0);
  ScopeLogger::GetInstance()->EnableScopesByName("-vpn");
  driver_->InitLoggingOptions(&options);
  ASSERT_EQ(1, options.size());
  EXPECT_EQ("--syslog", options[0]);
  ScopeLogger::GetInstance()->EnableScopesByName("+vpn");
  options.clear();
  driver_->InitLoggingOptions(&options);
  ExpectInFlags(options, "--verb", "3");
  ScopeLogger::GetInstance()->EnableScopesByName("-vpn");
  SetArg("OpenVPN.Verb", "2");
  options.clear();
  driver_->InitLoggingOptions(&options);
  ExpectInFlags(options, "--verb", "2");
  ScopeLogger::GetInstance()->EnableScopesByName("+vpn");
  SetArg("OpenVPN.Verb", "1");
  options.clear();
  driver_->InitLoggingOptions(&options);
  ExpectInFlags(options, "--verb", "1");
  if (!vpn_logging) {
    ScopeLogger::GetInstance()->EnableScopesByName("-vpn");
  }
}

TEST_F(OpenVPNDriverTest, AppendValueOption) {
  vector<string> options;
  EXPECT_FALSE(
      driver_->AppendValueOption("OpenVPN.UnknownProperty", kOption, &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, "");
  EXPECT_FALSE(driver_->AppendValueOption(kProperty, kOption, &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, kValue);
  SetArg(kProperty2, kValue2);
  EXPECT_TRUE(driver_->AppendValueOption(kProperty, kOption, &options));
  EXPECT_TRUE(driver_->AppendValueOption(kProperty2, kOption2, &options));
  EXPECT_EQ(4, options.size());
  EXPECT_EQ(kOption, options[0]);
  EXPECT_EQ(kValue, options[1]);
  EXPECT_EQ(kOption2, options[2]);
  EXPECT_EQ(kValue2, options[3]);
}

TEST_F(OpenVPNDriverTest, AppendFlag) {
  vector<string> options;
  EXPECT_FALSE(
      driver_->AppendFlag("OpenVPN.UnknownProperty", kOption, &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, "");
  SetArg(kProperty2, kValue2);
  EXPECT_TRUE(driver_->AppendFlag(kProperty, kOption, &options));
  EXPECT_TRUE(driver_->AppendFlag(kProperty2, kOption2, &options));
  EXPECT_EQ(2, options.size());
  EXPECT_EQ(kOption, options[0]);
  EXPECT_EQ(kOption2, options[1]);
}

TEST_F(OpenVPNDriverTest, ClaimInterface) {
  driver_->tunnel_interface_ = kInterfaceName;
  EXPECT_FALSE(driver_->ClaimInterface(string(kInterfaceName) + "XXX",
                                       kInterfaceIndex));
  EXPECT_FALSE(driver_->device_);

  static const char kHost[] = "192.168.2.254";
  SetArg(flimflam::kProviderHostProperty, kHost);
  EXPECT_CALL(*management_server_, Start(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsOnline()).WillOnce(Return(false));
  EXPECT_CALL(glib_, SpawnAsyncWithPipesCWD(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(glib_, ChildWatchAdd(_, _, _)).WillOnce(Return(1));
  const int kServiceCallbackTag = 1;
  EXPECT_EQ(0, driver_->default_service_callback_tag_);
  EXPECT_CALL(manager_, RegisterDefaultServiceCallback(_))
      .WillOnce(Return(kServiceCallbackTag));
  EXPECT_TRUE(driver_->ClaimInterface(kInterfaceName, kInterfaceIndex));
  ASSERT_TRUE(driver_->device_);
  EXPECT_EQ(kInterfaceIndex, driver_->device_->interface_index());
  EXPECT_EQ(kServiceCallbackTag, driver_->default_service_callback_tag_);
}

TEST_F(OpenVPNDriverTest, Cleanup) {
  driver_->Cleanup(Service::kStateIdle);  // Ensure no crash.

  const unsigned int kChildTag = 123;
  const int kPID = 123456;
  const int kServiceCallbackTag = 5;
  driver_->default_service_callback_tag_ = kServiceCallbackTag;
  driver_->child_watch_tag_ = kChildTag;
  driver_->pid_ = kPID;
  driver_->rpc_task_.reset(new RPCTask(&control_, this));
  driver_->tunnel_interface_ = kInterfaceName;
  driver_->device_ = device_;
  driver_->service_ = service_;
  driver_->ip_properties_.address = "1.2.3.4";
  driver_->StartConnectTimeout();
  FilePath tls_auth_file;
  EXPECT_TRUE(file_util::CreateTemporaryFile(&tls_auth_file));
  EXPECT_FALSE(tls_auth_file.empty());
  EXPECT_TRUE(file_util::PathExists(tls_auth_file));
  driver_->tls_auth_file_ = tls_auth_file;
  // Stop will be called twice -- once by Cleanup and once by the destructor.
  EXPECT_CALL(*management_server_, Stop()).Times(2);
  EXPECT_CALL(glib_, SourceRemove(kChildTag));
  EXPECT_CALL(manager_, DeregisterDefaultServiceCallback(kServiceCallbackTag));
  EXPECT_CALL(process_killer_, Kill(kPID, _));
  EXPECT_CALL(device_info_, DeleteInterface(_)).Times(0);
  EXPECT_CALL(*device_, OnDisconnected());
  EXPECT_CALL(*device_, SetEnabled(false));
  EXPECT_CALL(*service_, SetState(Service::kStateFailure));
  driver_->Cleanup(Service::kStateFailure);
  EXPECT_EQ(0, driver_->child_watch_tag_);
  EXPECT_EQ(0, driver_->default_service_callback_tag_);
  EXPECT_EQ(0, driver_->pid_);
  EXPECT_FALSE(driver_->rpc_task_.get());
  EXPECT_TRUE(driver_->tunnel_interface_.empty());
  EXPECT_FALSE(driver_->device_);
  EXPECT_FALSE(driver_->service_);
  EXPECT_FALSE(file_util::PathExists(tls_auth_file));
  EXPECT_TRUE(driver_->tls_auth_file_.empty());
  EXPECT_TRUE(driver_->ip_properties_.address.empty());
  EXPECT_FALSE(driver_->IsConnectTimeoutStarted());
}

namespace {
MATCHER(CheckEnv, "") {
  if (!arg || !arg[0] || !arg[1] || arg[2]) {
    return false;
  }
  return (string(arg[0]) == "IV_PLAT=Chromium OS" &&
          string(arg[1]) == "IV_PLAT_REL=2202.0");
}
}  // namespace

TEST_F(OpenVPNDriverTest, SpawnOpenVPN) {
  SetupLSBRelease();

  EXPECT_FALSE(driver_->SpawnOpenVPN());

  static const char kHost[] = "192.168.2.254";
  SetArg(flimflam::kProviderHostProperty, kHost);
  driver_->tunnel_interface_ = "tun0";
  driver_->rpc_task_.reset(new RPCTask(&control_, this));
  EXPECT_CALL(*management_server_, Start(_, _, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, IsOnline()).Times(2).WillRepeatedly(Return(false));

  const int kPID = 234678;
  EXPECT_CALL(glib_,
              SpawnAsyncWithPipesCWD(_, CheckEnv(), _, _, _, _, _, _, _, _))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgumentPointee<5>(kPID), Return(true)));
  const int kTag = 6;
  EXPECT_CALL(glib_, ChildWatchAdd(kPID, &driver_->OnOpenVPNDied, driver_))
      .WillOnce(Return(kTag));
  EXPECT_FALSE(driver_->SpawnOpenVPN());
  EXPECT_TRUE(driver_->SpawnOpenVPN());
  EXPECT_EQ(kPID, driver_->pid_);
  EXPECT_EQ(kTag, driver_->child_watch_tag_);
}

TEST_F(OpenVPNDriverTest, OnOpenVPNDied) {
  const int kPID = 99999;
  driver_->device_ = device_;
  driver_->child_watch_tag_ = 333;
  driver_->pid_ = kPID;
  EXPECT_CALL(*device_, OnDisconnected());
  EXPECT_CALL(*device_, SetEnabled(false));
  EXPECT_CALL(process_killer_, Kill(_, _)).Times(0);
  EXPECT_CALL(device_info_, DeleteInterface(kInterfaceIndex));
  OpenVPNDriver::OnOpenVPNDied(kPID, 2, driver_);
  EXPECT_EQ(0, driver_->child_watch_tag_);
  EXPECT_EQ(0, driver_->pid_);
}

TEST_F(OpenVPNDriverTest, Disconnect) {
  driver_->device_ = device_;
  driver_->service_ = service_;
  EXPECT_CALL(*device_, OnDisconnected());
  EXPECT_CALL(*device_, SetEnabled(false));
  EXPECT_CALL(device_info_, DeleteInterface(kInterfaceIndex));
  EXPECT_CALL(*service_, SetState(Service::kStateIdle));
  driver_->Disconnect();
  EXPECT_FALSE(driver_->device_);
  EXPECT_FALSE(driver_->service_);
}

TEST_F(OpenVPNDriverTest, OnConnectionDisconnected) {
  EXPECT_CALL(*management_server_, Restart());
  SetDevice(device_);
  SetService(service_);
  EXPECT_CALL(*device_, OnDisconnected());
  EXPECT_CALL(*service_, SetState(Service::kStateAssociating));
  OnConnectionDisconnected();
  EXPECT_TRUE(IsConnectTimeoutStarted());
}

TEST_F(OpenVPNDriverTest, OnConnectTimeout) {
  StartConnectTimeout();
  SetService(service_);
  EXPECT_CALL(*service_, SetState(Service::kStateFailure));
  OnConnectTimeout();
  EXPECT_FALSE(GetService());
  EXPECT_FALSE(IsConnectTimeoutStarted());
}

TEST_F(OpenVPNDriverTest, OnReconnecting) {
  driver_->OnReconnecting();  // Expect no crash.
  driver_->device_ = device_;
  driver_->service_ = service_;
  EXPECT_CALL(*device_, OnDisconnected());
  EXPECT_CALL(*service_, SetState(Service::kStateAssociating));
  driver_->OnReconnecting();
  EXPECT_TRUE(driver_->IsConnectTimeoutStarted());
}

TEST_F(OpenVPNDriverTest, VerifyPaths) {
  // Ensure that the various path constants that the OpenVPN driver uses
  // actually exists in the build image.  Due to build dependencies, they should
  // already exist by the time we run unit tests.

  // The OpenVPNDriver path constants are absolute.  FilePath::Append asserts
  // that its argument is not an absolute path, so we need to strip the leading
  // separators.  There's nothing built into FilePath to do so.
  static const char *kPaths[] = {
    OpenVPNDriver::kOpenVPNPath,
    OpenVPNDriver::kOpenVPNScript,
  };
  for (size_t i = 0; i < arraysize(kPaths); i++) {
    string path(kPaths[i]);
    TrimString(path, FilePath::kSeparators, &path);
    EXPECT_TRUE(file_util::PathExists(FilePath(SYSROOT).Append(path)))
        << kPaths[i];
  }
}

TEST_F(OpenVPNDriverTest, InitPropertyStore) {
  // Sanity test property store initialization.
  PropertyStore store;
  driver_->InitPropertyStore(&store);
  const string kUser = "joe";
  Error error;
  EXPECT_TRUE(
      store.SetStringProperty(flimflam::kOpenVPNUserProperty, kUser, &error));
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kUser, GetArgs()->LookupString(flimflam::kOpenVPNUserProperty, ""));
}

TEST_F(OpenVPNDriverTest, GetProvider) {
  PropertyStore store;
  driver_->InitPropertyStore(&store);
  {
    KeyValueStore props;
    Error error;
    EXPECT_TRUE(
        store.GetKeyValueStoreProperty(
            flimflam::kProviderProperty, &props, &error));
    EXPECT_TRUE(props.LookupBool(flimflam::kPassphraseRequiredProperty, false));
  }
  {
    KeyValueStore props;
    SetArg(flimflam::kOpenVPNPasswordProperty, "random-password");
    Error error;
    EXPECT_TRUE(
        store.GetKeyValueStoreProperty(
            flimflam::kProviderProperty, &props, &error));
    EXPECT_FALSE(props.LookupBool(flimflam::kPassphraseRequiredProperty, true));
    EXPECT_FALSE(props.ContainsString(flimflam::kOpenVPNPasswordProperty));
  }
}

TEST_F(OpenVPNDriverTest, ParseLSBRelease) {
  SetupLSBRelease();
  map<string, string> lsb_release;
  EXPECT_TRUE(driver_->ParseLSBRelease(&lsb_release));
  EXPECT_TRUE(ContainsKey(lsb_release, "foo") && lsb_release["foo"] == "");
  EXPECT_EQ("=", lsb_release["zoo"]);
  EXPECT_EQ("Chromium OS", lsb_release[OpenVPNDriver::kChromeOSReleaseName]);
  EXPECT_EQ("2202.0", lsb_release[OpenVPNDriver::kChromeOSReleaseVersion]);
  driver_->lsb_release_file_ = FilePath("/non/existent/file");
  EXPECT_FALSE(driver_->ParseLSBRelease(NULL));
}

TEST_F(OpenVPNDriverTest, InitEnvironment) {
  vector<string> env;
  SetupLSBRelease();
  driver_->InitEnvironment(&env);
  ASSERT_EQ(2, env.size());
  EXPECT_EQ("IV_PLAT=Chromium OS", env[0]);
  EXPECT_EQ("IV_PLAT_REL=2202.0", env[1]);
  env.clear();
  EXPECT_EQ(0, file_util::WriteFile(lsb_release_file_, "", 0));
  driver_->InitEnvironment(&env);
  EXPECT_EQ(0, env.size());
}

TEST_F(OpenVPNDriverTest, DeleteInterface) {
  scoped_ptr<MockDeviceInfo> device_info(
      new MockDeviceInfo(&control_, &dispatcher_, &metrics_, &manager_));
  EXPECT_CALL(*device_info, DeleteInterface(kInterfaceIndex))
      .WillOnce(Return(true));
  WeakPtr<DeviceInfo> weak = device_info->AsWeakPtr();
  EXPECT_TRUE(weak);
  OpenVPNDriver::DeleteInterface(weak, kInterfaceIndex);
  device_info.reset();
  EXPECT_FALSE(weak);
  // Expect no crash.
  OpenVPNDriver::DeleteInterface(weak, kInterfaceIndex);
}

TEST_F(OpenVPNDriverTest, OnDefaultServiceChanged) {
  driver_->service_ = service_;

  ServiceRefPtr null_service;
  EXPECT_CALL(*management_server_, Hold());
  driver_->OnDefaultServiceChanged(null_service);

  EXPECT_CALL(*management_server_, Hold());
  driver_->OnDefaultServiceChanged(service_);

  scoped_refptr<MockService> mock_service(
      new MockService(&control_, &dispatcher_, &metrics_, &manager_));

  EXPECT_CALL(*mock_service, IsConnected()).WillOnce(Return(false));
  EXPECT_CALL(*management_server_, Hold());
  driver_->OnDefaultServiceChanged(mock_service);

  EXPECT_CALL(*mock_service, IsConnected()).WillOnce(Return(true));
  EXPECT_CALL(*management_server_, ReleaseHold());
  driver_->OnDefaultServiceChanged(mock_service);
}

}  // namespace shill

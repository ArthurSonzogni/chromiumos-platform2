// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcpv4_config.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/minijail/mock_minijail.h>

#include "shill/dbus_adaptor.h"
#include "shill/dhcp/dhcp_provider.h"
#include "shill/dhcp/mock_dhcp_proxy.h"
#include "shill/event_dispatcher.h"
#include "shill/mock_control.h"
#include "shill/mock_glib.h"
#include "shill/mock_log.h"
#include "shill/mock_metrics.h"
#include "shill/mock_proxy_factory.h"
#include "shill/property_store_unittest.h"
#include "shill/testing.h"

using base::Bind;
using base::FilePath;
using base::ScopedTempDir;
using base::Unretained;
using chromeos::MockMinijail;
using std::string;
using std::unique_ptr;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::ContainsRegex;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::SetArgumentPointee;
using testing::Test;

namespace shill {

namespace {
const char kDeviceName[] = "eth0";
const char kHostName[] = "hostname";
const char kLeaseFileSuffix[] = "leasefilesuffix";
const bool kArpGateway = true;
const bool kHasHostname = true;
const bool kHasLeaseSuffix = true;
}  // namespace

typedef scoped_refptr<DHCPv4Config> DHCPv4ConfigRefPtr;

class DHCPv4ConfigTest : public PropertyStoreTest {
 public:
  DHCPv4ConfigTest()
      : proxy_(new MockDHCPProxy()),
        minijail_(new MockMinijail()),
        metrics_(dispatcher()),
        config_(new DHCPv4Config(&control_,
                                dispatcher(),
                                DHCPProvider::GetInstance(),
                                kDeviceName,
                                kHostName,
                                kLeaseFileSuffix,
                                kArpGateway,
                                glib(),
                                &metrics_)) {}

  virtual void SetUp() {
    config_->proxy_factory_ = &proxy_factory_;
    config_->minijail_ = minijail_.get();
  }

  virtual void TearDown() {
    config_->proxy_factory_ = nullptr;
    config_->minijail_ = nullptr;
  }

  bool StartInstance(DHCPv4ConfigRefPtr config) {
    return config->Start();
  }

  void StopInstance() {
    config_->Stop("In test");
  }

  DHCPv4ConfigRefPtr CreateMockMinijailConfig(const string &hostname,
                                              const string &lease_suffix,
                                              bool arp_gateway);
  DHCPv4ConfigRefPtr CreateRunningConfig(const string &hostname,
                                         const string &lease_suffix,
                                         bool arp_gateway);
  void StopRunningConfigAndExpect(DHCPv4ConfigRefPtr config,
                                  bool lease_file_exists);

 protected:
  static const int kPID;
  static const unsigned int kTag;

  FilePath lease_file_;
  FilePath pid_file_;
  ScopedTempDir temp_dir_;
  unique_ptr<MockDHCPProxy> proxy_;
  MockProxyFactory proxy_factory_;
  MockControl control_;
  unique_ptr<MockMinijail> minijail_;
  MockMetrics metrics_;
  DHCPv4ConfigRefPtr config_;
};

const int DHCPv4ConfigTest::kPID = 123456;
const unsigned int DHCPv4ConfigTest::kTag = 77;

DHCPv4ConfigRefPtr DHCPv4ConfigTest::CreateMockMinijailConfig(
    const string &hostname,
    const string &lease_suffix,
    bool arp_gateway) {
  DHCPv4ConfigRefPtr config(new DHCPv4Config(&control_,
                                            dispatcher(),
                                            DHCPProvider::GetInstance(),
                                            kDeviceName,
                                            hostname,
                                            lease_suffix,
                                            arp_gateway,
                                            glib(),
                                            &metrics_));
  config->minijail_ = minijail_.get();

  return config;
}

DHCPv4ConfigRefPtr DHCPv4ConfigTest::CreateRunningConfig(
    const string &hostname, const string &lease_suffix, bool arp_gateway) {
  DHCPv4ConfigRefPtr config(new DHCPv4Config(&control_,
                                            dispatcher(),
                                            DHCPProvider::GetInstance(),
                                            kDeviceName,
                                            hostname,
                                            lease_suffix,
                                            arp_gateway,
                                            glib(),
                                            &metrics_));
  config->minijail_ = minijail_.get();
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _))
      .WillOnce(DoAll(SetArgumentPointee<2>(kPID), Return(true)));
  EXPECT_CALL(*glib(), ChildWatchAdd(kPID, _, _)).WillOnce(Return(kTag));
  EXPECT_TRUE(config->Start());
  EXPECT_EQ(kPID, config->pid_);
  EXPECT_EQ(config.get(), DHCPProvider::GetInstance()->GetConfig(kPID).get());
  EXPECT_EQ(kTag, config->child_watch_tag_);

  EXPECT_TRUE(temp_dir_.CreateUniqueTempDir());
  config->root_ = temp_dir_.path();
  FilePath varrun = temp_dir_.path().Append("var/run/dhcpcd");
  EXPECT_TRUE(base::CreateDirectory(varrun));
  pid_file_ = varrun.Append(base::StringPrintf("dhcpcd-%s-4.pid", kDeviceName));
  FilePath varlib = temp_dir_.path().Append("var/lib/dhcpcd");
  EXPECT_TRUE(base::CreateDirectory(varlib));
  lease_file_ =
      varlib.Append(base::StringPrintf("dhcpcd-%s.lease", kDeviceName));
  EXPECT_EQ(0, base::WriteFile(pid_file_, "", 0));
  EXPECT_EQ(0, base::WriteFile(lease_file_, "", 0));
  EXPECT_TRUE(base::PathExists(pid_file_));
  EXPECT_TRUE(base::PathExists(lease_file_));

  return config;
}

void DHCPv4ConfigTest::StopRunningConfigAndExpect(DHCPv4ConfigRefPtr config,
                                                  bool lease_file_exists) {
  ScopedMockLog log;
  // We use a non-zero exit status so that we get the log message.
  EXPECT_CALL(log, Log(_, _, ::testing::EndsWith("status 10")));
  DHCPConfig::ChildWatchCallback(kPID, 10, config.get());
  EXPECT_EQ(nullptr, DHCPProvider::GetInstance()->GetConfig(kPID).get());

  EXPECT_FALSE(base::PathExists(pid_file_));
  EXPECT_EQ(lease_file_exists, base::PathExists(lease_file_));
}

TEST_F(DHCPv4ConfigTest, GetIPv4AddressString) {
  EXPECT_EQ("255.255.255.255", config_->GetIPv4AddressString(0xffffffff));
  EXPECT_EQ("0.0.0.0", config_->GetIPv4AddressString(0));
  EXPECT_EQ("1.2.3.4", config_->GetIPv4AddressString(0x04030201));
}

TEST_F(DHCPv4ConfigTest, ParseClasslessStaticRoutes) {
  const string kDefaultAddress = "0.0.0.0";
  const string kDefaultDestination = kDefaultAddress + "/0";
  const string kRouter0 = "10.0.0.254";
  const string kAddress1 = "192.168.1.0";
  const string kDestination1 = kAddress1 + "/24";
  // Last gateway missing, leaving an odd number of parameters.
  const string kBrokenClasslessRoutes0 = kDefaultDestination + " " + kRouter0 +
      " " + kDestination1;
  IPConfig::Properties properties;
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes0,
                                                        &properties));
  EXPECT_TRUE(properties.routes.empty());
  EXPECT_TRUE(properties.gateway.empty());

  // Gateway argument for the second route is malformed, but we were able
  // to salvage a default gateway.
  const string kBrokenRouter1 = "10.0.0";
  const string kBrokenClasslessRoutes1 = kBrokenClasslessRoutes0 + " " +
      kBrokenRouter1;
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes1,
                                                        &properties));
  EXPECT_TRUE(properties.routes.empty());
  EXPECT_EQ(kRouter0, properties.gateway);

  const string kRouter1 = "10.0.0.253";
  const string kRouter2 = "10.0.0.252";
  const string kClasslessRoutes0 = kDefaultDestination + " " + kRouter2 + " " +
      kDestination1 + " " + kRouter1;
  EXPECT_TRUE(DHCPv4Config::ParseClasslessStaticRoutes(kClasslessRoutes0,
                                                       &properties));
  // The old default route is preserved.
  EXPECT_EQ(kRouter0, properties.gateway);

  // The two routes (including the one which would have otherwise been
  // classified as a default route) are added to the routing table.
  EXPECT_EQ(2, properties.routes.size());
  const IPConfig::Route &route0 = properties.routes[0];
  EXPECT_EQ(kDefaultAddress, route0.host);
  EXPECT_EQ("0.0.0.0", route0.netmask);
  EXPECT_EQ(kRouter2, route0.gateway);

  const IPConfig::Route &route1 = properties.routes[1];
  EXPECT_EQ(kAddress1, route1.host);
  EXPECT_EQ("255.255.255.0", route1.netmask);
  EXPECT_EQ(kRouter1, route1.gateway);

  // A malformed routing table should not affect the current table.
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes1,
                                                        &properties));
  EXPECT_EQ(2, properties.routes.size());
  EXPECT_EQ(kRouter0, properties.gateway);
}

TEST_F(DHCPv4ConfigTest, ParseConfiguration) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
      0x01020304);
  conf[DHCPv4Config::kConfigurationKeySubnetCIDR].writer().append_byte(
      16);
  conf[DHCPv4Config::kConfigurationKeyBroadcastAddress].writer().append_uint32(
      0x10203040);
  {
    vector<unsigned int> routers;
    routers.push_back(0x02040608);
    routers.push_back(0x03050709);
    DBus::MessageIter writer =
        conf[DHCPv4Config::kConfigurationKeyRouters].writer();
    writer << routers;
  }
  {
    vector<unsigned int> dns;
    dns.push_back(0x09070503);
    dns.push_back(0x08060402);
    DBus::MessageIter writer =
        conf[DHCPv4Config::kConfigurationKeyDNS].writer();
    writer << dns;
  }
  conf[DHCPv4Config::kConfigurationKeyDomainName].writer().append_string(
      "domain-name");
  {
    vector<string> search;
    search.push_back("foo.com");
    search.push_back("bar.com");
    DBus::MessageIter writer =
        conf[DHCPv4Config::kConfigurationKeyDomainSearch].writer();
    writer << search;
  }
  conf[DHCPv4Config::kConfigurationKeyMTU].writer().append_uint16(600);
  conf[DHCPv4Config::kConfigurationKeyHostname].writer().append_string(
      "hostname");
  conf["UnknownKey"] = DBus::Variant();

  IPConfig::Properties properties;
  ASSERT_TRUE(config_->ParseConfiguration(conf, &properties));
  EXPECT_EQ("4.3.2.1", properties.address);
  EXPECT_EQ(16, properties.subnet_prefix);
  EXPECT_EQ("64.48.32.16", properties.broadcast_address);
  EXPECT_EQ("8.6.4.2", properties.gateway);
  ASSERT_EQ(2, properties.dns_servers.size());
  EXPECT_EQ("3.5.7.9", properties.dns_servers[0]);
  EXPECT_EQ("2.4.6.8", properties.dns_servers[1]);
  EXPECT_EQ("domain-name", properties.domain_name);
  ASSERT_EQ(2, properties.domain_search.size());
  EXPECT_EQ("foo.com", properties.domain_search[0]);
  EXPECT_EQ("bar.com", properties.domain_search[1]);
  EXPECT_EQ(600, properties.mtu);
  EXPECT_EQ("hostname", properties.accepted_hostname);
}

TEST_F(DHCPv4ConfigTest, ParseConfigurationWithMinimumMTU) {
  config_->set_minimum_mtu(1500);

  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyMTU].writer().append_uint16(576);

  IPConfig::Properties properties;
  ASSERT_TRUE(config_->ParseConfiguration(conf, &properties));
  EXPECT_EQ(IPConfig::kUndefinedMTU, properties.mtu);
}

MATCHER_P3(IsDHCPCDArgs, has_hostname, has_arp_gateway, has_lease_suffix, "") {
  if (string(arg[0]) != "/sbin/dhcpcd" ||
      string(arg[1]) != "-B" ||
      string(arg[2]) != "-q" ||
      string(arg[3]) != "-4") {
    return false;
  }

  int end_offset = 4;
  if (has_hostname) {
    if (string(arg[end_offset]) != "-h" ||
        string(arg[end_offset + 1]) != kHostName) {
      return false;
    }
    end_offset += 2;
  }

  if (has_arp_gateway) {
    if (string(arg[end_offset]) != "-R" ||
        string(arg[end_offset + 1]) != "-P") {
      return false;
    }
    end_offset += 2;
  }

  string device_arg = has_lease_suffix ?
      string(kDeviceName) + "=" + string(kLeaseFileSuffix) : kDeviceName;
  return string(arg[end_offset]) == device_arg &&
         arg[end_offset + 1] == nullptr;
}

TEST_F(DHCPv4ConfigTest, StartWithHostname) {
  EXPECT_CALL(*minijail_, RunAndDestroy(_, IsDHCPCDArgs(kHasHostname,
                                                        kArpGateway,
                                                        kHasLeaseSuffix), _))
      .WillOnce(Return(false));
  EXPECT_FALSE(StartInstance(config_));
}

TEST_F(DHCPv4ConfigTest, StartWithoutHostname) {
  DHCPv4ConfigRefPtr config = CreateMockMinijailConfig("",
                                                       kLeaseFileSuffix,
                                                       kArpGateway);
  EXPECT_CALL(*minijail_, RunAndDestroy(_, IsDHCPCDArgs(!kHasHostname,
                                                        kArpGateway,
                                                        kHasLeaseSuffix), _))
      .WillOnce(Return(false));
  EXPECT_FALSE(StartInstance(config));
}

TEST_F(DHCPv4ConfigTest, StartWithoutArpGateway) {
  DHCPv4ConfigRefPtr config = CreateMockMinijailConfig(kHostName,
                                                      kLeaseFileSuffix,
                                                      !kArpGateway);
  EXPECT_CALL(*minijail_, RunAndDestroy(_, IsDHCPCDArgs(kHasHostname,
                                                        !kArpGateway,
                                                        kHasLeaseSuffix), _))
      .WillOnce(Return(false));
  EXPECT_FALSE(StartInstance(config));
}

namespace {

class DHCPv4ConfigCallbackTest : public DHCPv4ConfigTest {
 public:
  virtual void SetUp() {
    DHCPv4ConfigTest::SetUp();
    config_->RegisterUpdateCallback(
        Bind(&DHCPv4ConfigCallbackTest::SuccessCallback, Unretained(this)));
    config_->RegisterFailureCallback(
        Bind(&DHCPv4ConfigCallbackTest::FailureCallback, Unretained(this)));
    ip_config_ = config_;
  }

  MOCK_METHOD2(SuccessCallback,
               void(const IPConfigRefPtr &ipconfig, bool new_lease_acquired));
  MOCK_METHOD1(FailureCallback, void(const IPConfigRefPtr &ipconfig));

  // The mock methods above take IPConfigRefPtr because this is the type
  // that the registered callbacks take.  This conversion of the DHCP
  // config ref pointer eases our work in setting up expectations.
  const IPConfigRefPtr &ConfigRef() { return ip_config_; }

 private:
  IPConfigRefPtr ip_config_;
};

}  // namespace

TEST_F(DHCPv4ConfigCallbackTest, ProcessEventSignalFail) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
      0x01020304);
  EXPECT_CALL(*this, SuccessCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->ProcessEventSignal(DHCPv4Config::kReasonFail, conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_TRUE(config_->properties().address.empty());
}

TEST_F(DHCPv4ConfigCallbackTest, ProcessEventSignalSuccess) {
  for (const auto &reason : { DHCPv4Config::kReasonBound,
                              DHCPv4Config::kReasonRebind,
                              DHCPv4Config::kReasonReboot,
                              DHCPv4Config::kReasonRenew }) {
    int address_octet = 0;
    for (const auto lease_time_given : { false, true }) {
      DHCPConfig::Configuration conf;
      conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
        ++address_octet);
      if (lease_time_given) {
        const uint32_t kLeaseTime = 1;
        conf[DHCPv4Config::kConfigurationKeyLeaseTime].writer().append_uint32(
            kLeaseTime);
      }
      EXPECT_CALL(*this, SuccessCallback(ConfigRef(), true));
      EXPECT_CALL(*this, FailureCallback(_)).Times(0);
      config_->ProcessEventSignal(reason, conf);
      string failure_message = string(reason) + " failed with lease time " +
          (lease_time_given ? "given" : "not given");
      EXPECT_TRUE(Mock::VerifyAndClearExpectations(this)) << failure_message;
      EXPECT_EQ(base::StringPrintf("%d.0.0.0", address_octet),
                config_->properties().address) << failure_message;
    }
  }
}

TEST_F(DHCPv4ConfigCallbackTest, StoppedDuringFailureCallback) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
    0x01020304);
  // Stop the DHCP config while it is calling the failure callback.  We
  // need to ensure that no callbacks are left running inadvertently as
  // a result.
  EXPECT_CALL(*this, FailureCallback(ConfigRef()))
      .WillOnce(InvokeWithoutArgs(this, &DHCPv4ConfigTest::StopInstance));
  config_->ProcessEventSignal(DHCPv4Config::kReasonFail, conf);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(this));
}

TEST_F(DHCPv4ConfigCallbackTest, StoppedDuringSuccessCallback) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
    0x01020304);
  const uint32_t kLeaseTime = 1;
  conf[DHCPv4Config::kConfigurationKeyLeaseTime].writer().append_uint32(
      kLeaseTime);
  // Stop the DHCP config while it is calling the success callback.  This
  // can happen if the device has a static IP configuration and releases
  // the lease after accepting other network parameters from the DHCP
  // IPConfig properties.  We need to ensure that no callbacks are left
  // running inadvertently as a result.
  EXPECT_CALL(*this, SuccessCallback(ConfigRef(), true))
      .WillOnce(InvokeWithoutArgs(this, &DHCPv4ConfigTest::StopInstance));
  config_->ProcessEventSignal(DHCPv4Config::kReasonBound, conf);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(this));
}

TEST_F(DHCPv4ConfigCallbackTest, ProcessEventSignalUnknown) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
      0x01020304);
  static const char kReasonUnknown[] = "UNKNOWN_REASON";
  EXPECT_CALL(*this, SuccessCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  config_->ProcessEventSignal(kReasonUnknown, conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_TRUE(config_->properties().address.empty());
}

TEST_F(DHCPv4ConfigCallbackTest, ProcessEventSignalGatewayArp) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
      0x01020304);
  EXPECT_CALL(*this, SuccessCallback(ConfigRef(), false));
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(Return(true));
  StartInstance(config_);
  config_->ProcessEventSignal(DHCPv4Config::kReasonGatewayArp, conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_EQ("4.3.2.1", config_->properties().address);
  EXPECT_TRUE(config_->is_gateway_arp_active_);
  // Will not fail on acquisition timeout since Gateway ARP is active.
  EXPECT_FALSE(config_->ShouldFailOnAcquisitionTimeout());

  // An official reply from a DHCP server should reset our GatewayArp state.
  EXPECT_CALL(*this, SuccessCallback(ConfigRef(), true));
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  config_->ProcessEventSignal(DHCPv4Config::kReasonRenew, conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_FALSE(config_->is_gateway_arp_active_);
  // Will fail on acquisition timeout since Gateway ARP is not active.
  EXPECT_TRUE(config_->ShouldFailOnAcquisitionTimeout());
}

TEST_F(DHCPv4ConfigCallbackTest, ProcessEventSignalGatewayArpNak) {
  DHCPConfig::Configuration conf;
  conf[DHCPv4Config::kConfigurationKeyIPAddress].writer().append_uint32(
      0x01020304);
  EXPECT_CALL(*minijail_, RunAndDestroy(_, _, _)).WillOnce(Return(true));
  StartInstance(config_);
  config_->ProcessEventSignal(DHCPv4Config::kReasonGatewayArp, conf);
  EXPECT_TRUE(config_->is_gateway_arp_active_);

  // Sending a NAK should clear is_gateway_arp_active_.
  config_->ProcessEventSignal(DHCPv4Config::kReasonNak, conf);
  EXPECT_FALSE(config_->is_gateway_arp_active_);
  // Will fail on acquisition timeout since Gateway ARP is not active.
  EXPECT_TRUE(config_->ShouldFailOnAcquisitionTimeout());
  Mock::VerifyAndClearExpectations(this);
}

TEST_F(DHCPv4ConfigTest, ProcessStatusChangeSingal) {
  EXPECT_CALL(metrics_, NotifyDhcpClientStatus(
      Metrics::kDhcpClientStatusBound));
  config_->ProcessStatusChangeSignal(DHCPv4Config::kStatusBound);
}

TEST_F(DHCPv4ConfigTest, StartSuccessEphemeral) {
  DHCPv4ConfigRefPtr config =
      CreateRunningConfig(kHostName, kDeviceName, kArpGateway);
  StopRunningConfigAndExpect(config, false);
}

TEST_F(DHCPv4ConfigTest, StartSuccessPersistent) {
  DHCPv4ConfigRefPtr config =
      CreateRunningConfig(kHostName, kLeaseFileSuffix, kArpGateway);
  StopRunningConfigAndExpect(config, true);
}

}  // namespace shill

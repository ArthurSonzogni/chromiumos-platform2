// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_config.h"

#include <memory>
#include <string>
#include <sys/time.h>
#include <utility>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/mock_log.h"
#include "shill/mock_metrics.h"
#include "shill/mock_process_manager.h"
#include "shill/net/mock_time.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/mock_dhcp_provider.h"
#include "shill/network/mock_dhcp_proxy.h"
#include "shill/store/property_store_test.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::ContainsRegex;
using testing::DoAll;
using testing::EndsWith;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;

namespace shill {

namespace {
constexpr bool kArpGateway = true;
constexpr char kDeviceName[] = "eth0";
constexpr char kHostName[] = "hostname";
constexpr char kLeaseFileSuffix[] = "leasefilesuffix";
constexpr bool kHasHostname = true;
constexpr bool kHasLeaseSuffix = true;
constexpr uint32_t kTimeNow = 10;
constexpr uint32_t kLeaseDuration = 5;
}  // namespace

class DHCPConfigTest : public PropertyStoreTest {
 public:
  DHCPConfigTest()
      : proxy_(new MockDHCPProxy()),
        config_(new DHCPv4Config(control_interface(),
                                 dispatcher(),
                                 &provider_,
                                 kDeviceName,
                                 kLeaseFileSuffix,
                                 kArpGateway,
                                 kHostName,
                                 Technology::kUnknown,
                                 &metrics_)) {
    config_->time_ = &time_;
  }

  void SetUp() override {
    config_->process_manager_ = &process_manager_;
    ScopeLogger::GetInstance()->EnableScopesByName("dhcp");
    ScopeLogger::GetInstance()->set_verbose_level(3);
  }

  void TearDown() override {
    ScopeLogger::GetInstance()->EnableScopesByName("-dhcp");
    ScopeLogger::GetInstance()->set_verbose_level(0);
  }

  // Sets the current time returned by time_.GetTimeBoottime() to |second|.
  void SetCurrentTimeToSecond(uint32_t second) {
    struct timeval current = {static_cast<__time_t>(second), 0};
    EXPECT_CALL(time_, GetTimeBoottime(_))
        .WillOnce(DoAll(SetArgPointee<0>(current), Return(0)));
  }

  bool StartInstance() { return config_->Start(); }

  void StopInstance() { config_->Stop("In test"); }

  void InvokeOnIPConfigUpdated(const IPConfig::Properties& properties,
                               bool new_lease_acquired) {
    config_->OnIPConfigUpdated(properties, new_lease_acquired);
  }

  bool ShouldFailOnAcquisitionTimeout() {
    return config_->ShouldFailOnAcquisitionTimeout();
  }

  void SetShouldFailOnAcquisitionTimeout(bool value) {
    config_->is_gateway_arp_active_ = !value;
  }

  bool ShouldKeepLeaseOnDisconnect() {
    return config_->ShouldKeepLeaseOnDisconnect();
  }

  void SetShouldKeepLeaseOnDisconnect(bool value) {
    config_->arp_gateway_ = value;
  }

  void CreateMockMinijailConfig(const std::string& hostname,
                                const std::string& lease_suffix,
                                bool arp_gateway);

 protected:
  static constexpr int kPID = 123456;

  std::unique_ptr<MockDHCPProxy> proxy_;
  MockProcessManager process_manager_;
  MockTime time_;
  scoped_refptr<DHCPv4Config> config_;
  MockDHCPProvider provider_;
  MockMetrics metrics_;
};

// Resets |config_| to an instance initiated with the given parameters, which
// can be used in the tests for verifying parameters to invoke minijail.
void DHCPConfigTest::CreateMockMinijailConfig(const std::string& hostname,
                                              const std::string& lease_suffix,
                                              bool arp_gateway) {
  config_ = new DHCPv4Config(control_interface(), dispatcher(), &provider_,
                             kDeviceName, lease_suffix, arp_gateway, hostname,
                             Technology::kUnknown, metrics());
  config_->process_manager_ = &process_manager_;
}

TEST_F(DHCPConfigTest, InitProxy) {
  static const char kService[] = ":1.200";
  EXPECT_NE(nullptr, proxy_);
  EXPECT_EQ(nullptr, config_->proxy_);
  EXPECT_CALL(*control_interface(), CreateDHCPProxy(kService))
      .WillOnce(Return(ByMove(std::move(proxy_))));
  config_->InitProxy(kService);
  EXPECT_EQ(nullptr, proxy_);
  EXPECT_NE(nullptr, config_->proxy_);

  config_->InitProxy(kService);
}

TEST_F(DHCPConfigTest, StartFail) {
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(-1));
  EXPECT_FALSE(config_->Start());
  EXPECT_EQ(0, config_->pid_);
}

MATCHER_P3(IsDHCPCDArgs, has_hostname, has_arp_gateway, has_lease_suffix, "") {
  if (arg[0] != "-B" || arg[1] != "-q" || arg[2] != "-4") {
    return false;
  }

  int end_offset = 3;
  if (has_hostname) {
    if (arg[end_offset] != "-h" || arg[end_offset + 1] != kHostName) {
      return false;
    }
    end_offset += 2;
  }

  if (has_arp_gateway) {
    if (arg[end_offset] != "-R" || arg[end_offset + 1] != "--unicast") {
      return false;
    }
    end_offset += 2;
  }

  std::string device_arg = has_lease_suffix ? std::string(kDeviceName) + "=" +
                                                  std::string(kLeaseFileSuffix)
                                            : kDeviceName;
  return arg[end_offset] == device_arg;
}

TEST_F(DHCPConfigTest, StartWithoutLeaseSuffix) {
  CreateMockMinijailConfig(kHostName, kDeviceName, kArpGateway);
  EXPECT_CALL(
      process_manager_,
      StartProcessInMinijail(
          _, _, IsDHCPCDArgs(kHasHostname, kArpGateway, !kHasLeaseSuffix), _, _,
          _))
      .WillOnce(Return(-1));
  EXPECT_FALSE(StartInstance());
}

TEST_F(DHCPConfigTest, StartWithHostname) {
  CreateMockMinijailConfig(kHostName, kLeaseFileSuffix, kArpGateway);
  EXPECT_CALL(
      process_manager_,
      StartProcessInMinijail(
          _, _, IsDHCPCDArgs(kHasHostname, kArpGateway, kHasLeaseSuffix), _, _,
          _))
      .WillOnce(Return(-1));
  EXPECT_FALSE(StartInstance());
}

TEST_F(DHCPConfigTest, StartWithEmptyHostname) {
  CreateMockMinijailConfig("", kLeaseFileSuffix, kArpGateway);
  EXPECT_CALL(
      process_manager_,
      StartProcessInMinijail(
          _, _, IsDHCPCDArgs(!kHasHostname, kArpGateway, kHasLeaseSuffix), _, _,
          _))
      .WillOnce(Return(-1));
  EXPECT_FALSE(StartInstance());
}

TEST_F(DHCPConfigTest, StartWithoutArpGateway) {
  CreateMockMinijailConfig(kHostName, kLeaseFileSuffix, !kArpGateway);
  EXPECT_CALL(
      process_manager_,
      StartProcessInMinijail(
          _, _, IsDHCPCDArgs(kHasHostname, !kArpGateway, kHasLeaseSuffix), _, _,
          _))
      .WillOnce(Return(-1));
  EXPECT_FALSE(StartInstance());
}

TEST_F(DHCPConfigTest, TimeToLeaseExpiry_Success) {
  IPConfig::Properties properties;
  properties.lease_duration_seconds = kLeaseDuration;
  SetCurrentTimeToSecond(kTimeNow);
  InvokeOnIPConfigUpdated(properties, true);

  for (uint32_t i = 0; i < kLeaseDuration; i++) {
    SetCurrentTimeToSecond(kTimeNow + i);
    EXPECT_EQ(base::Seconds(kLeaseDuration - i), config_->TimeToLeaseExpiry());
  }
}

TEST_F(DHCPConfigTest, TimeToLeaseExpiry_NoDHCPLease) {
  ScopedMockLog log;
  // |current_lease_expiration_time_| has not been set, so expect an error.
  EXPECT_CALL(log, Log(_, _, EndsWith("No current DHCP lease")));
  EXPECT_FALSE(config_->TimeToLeaseExpiry().has_value());
}

TEST_F(DHCPConfigTest, TimeToLeaseExpiry_CurrentLeaseExpired) {
  IPConfig::Properties properties;
  properties.lease_duration_seconds = kLeaseDuration;
  SetCurrentTimeToSecond(kTimeNow);
  InvokeOnIPConfigUpdated(properties, true);

  // Lease should expire at kTimeNow + kLeaseDuration.
  ScopedMockLog log;
  SetCurrentTimeToSecond(kTimeNow + kLeaseDuration + 1);
  EXPECT_CALL(log,
              Log(_, _, EndsWith("Current DHCP lease has already expired")));
  EXPECT_FALSE(config_->TimeToLeaseExpiry().has_value());
}

TEST_F(DHCPConfigTest, ExpiryMetrics) {
  // Get a lease with duration of 1 second, the expiry callback should be
  // triggered right after 1 second.
  IPConfig::Properties properties;
  properties.lease_duration_seconds = 1;
  InvokeOnIPConfigUpdated(properties, true);

  dispatcher()->task_environment().FastForwardBy(base::Milliseconds(500));

  EXPECT_CALL(metrics_,
              SendToUMA("Network.Shill.Unknown.ExpiredLeaseLengthSeconds2", 1,
                        Metrics::kMetricExpiredLeaseLengthSecondsMin,
                        Metrics::kMetricExpiredLeaseLengthSecondsMax,
                        Metrics::kMetricExpiredLeaseLengthSecondsNumBuckets));
  dispatcher()->task_environment().FastForwardBy(base::Milliseconds(500));
}

namespace {

class DHCPConfigCallbackTest : public DHCPConfigTest {
 public:
  void SetUp() override {
    DHCPConfigTest::SetUp();
    config_->RegisterCallbacks(
        base::BindRepeating(&DHCPConfigCallbackTest::UpdateCallback,
                            base::Unretained(this)),
        base::BindRepeating(&DHCPConfigCallbackTest::FailureCallback,
                            base::Unretained(this)));
    ip_config_ = config_;
  }

  MOCK_METHOD(void, UpdateCallback, (const IPConfigRefPtr&, bool));
  MOCK_METHOD(void, FailureCallback, (const IPConfigRefPtr&));

  // The mock methods above take IPConfigRefPtr because this is the type
  // that the registered callbacks take.  This conversion of the DHCP
  // config ref pointer eases our work in setting up expectations.
  const IPConfigRefPtr& ConfigRef() { return ip_config_; }

 protected:
  IPConfigRefPtr ip_config_;
};

}  // namespace

TEST_F(DHCPConfigCallbackTest, ProcessEventSignalSuccess) {
  for (const auto& reason :
       {DHCPv4Config::kReasonBound, DHCPv4Config::kReasonRebind,
        DHCPv4Config::kReasonReboot, DHCPv4Config::kReasonRenew}) {
    int address_octet = 0;
    for (const auto lease_time_given : {false, true}) {
      KeyValueStore conf;
      conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress,
                         ++address_octet);
      if (lease_time_given) {
        const uint32_t kLeaseTime = 1;
        conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyLeaseTime,
                           kLeaseTime);
      }
      EXPECT_CALL(*this, UpdateCallback(ConfigRef(), true));
      EXPECT_CALL(*this, FailureCallback(_)).Times(0);
      config_->ProcessEventSignal(reason, conf);
      std::string failure_message = std::string(reason) +
                                    " failed with lease time " +
                                    (lease_time_given ? "given" : "not given");
      EXPECT_TRUE(Mock::VerifyAndClearExpectations(this)) << failure_message;
      EXPECT_EQ(base::StringPrintf("%d.0.0.0", address_octet),
                config_->properties().address)
          << failure_message;
    }
  }
}

TEST_F(DHCPConfigCallbackTest, ProcessEventSignalFail) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  EXPECT_CALL(*this, UpdateCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->lease_acquisition_timeout_callback_.Reset(base::DoNothing());
  config_->lease_expiration_callback_.Reset(base::DoNothing());
  config_->ProcessEventSignal(DHCPv4Config::kReasonFail, conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_TRUE(config_->properties().address.empty());
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
}

TEST_F(DHCPConfigCallbackTest, ProcessEventSignalUnknown) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  EXPECT_CALL(*this, UpdateCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  config_->ProcessEventSignal("unknown", conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_TRUE(config_->properties().address.empty());
}

TEST_F(DHCPConfigCallbackTest, ProcessEventSignalGatewayArp) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  EXPECT_CALL(*this, UpdateCallback(ConfigRef(), false));
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(0));
  StartInstance();
  config_->ProcessEventSignal(DHCPv4Config::kReasonGatewayArp, conf);
  Mock::VerifyAndClearExpectations(this);
  EXPECT_EQ("4.3.2.1", config_->properties().address);
  // Will not fail on acquisition timeout since Gateway ARP is active.
  EXPECT_FALSE(ShouldFailOnAcquisitionTimeout());

  // An official reply from a DHCP server should reset our GatewayArp state.
  EXPECT_CALL(*this, UpdateCallback(ConfigRef(), true));
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  config_->ProcessEventSignal(DHCPv4Config::kReasonRenew, conf);
  Mock::VerifyAndClearExpectations(this);
  // Will fail on acquisition timeout since Gateway ARP is not active.
  EXPECT_TRUE(ShouldFailOnAcquisitionTimeout());
}

TEST_F(DHCPConfigCallbackTest, ProcessEventSignalGatewayArpNak) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(0));
  StartInstance();
  config_->ProcessEventSignal(DHCPv4Config::kReasonGatewayArp, conf);
  EXPECT_FALSE(ShouldFailOnAcquisitionTimeout());

  // Sending a NAK should clear is_gateway_arp_active_.
  config_->ProcessEventSignal(DHCPv4Config::kReasonNak, conf);
  // Will fail on acquisition timeout since Gateway ARP is not active.
  EXPECT_TRUE(ShouldFailOnAcquisitionTimeout());
  Mock::VerifyAndClearExpectations(this);
}

TEST_F(DHCPConfigCallbackTest, StoppedDuringFailureCallback) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  // Stop the DHCP config while it is calling the failure callback.  We
  // need to ensure that no callbacks are left running inadvertently as
  // a result.
  EXPECT_CALL(*this, FailureCallback(ConfigRef()))
      .WillOnce(InvokeWithoutArgs(this, &DHCPConfigTest::StopInstance));
  config_->ProcessEventSignal(DHCPv4Config::kReasonFail, conf);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(this));
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
}

TEST_F(DHCPConfigCallbackTest, StoppedDuringSuccessCallback) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyLeaseTime, kLeaseDuration);

  // Stop the DHCP config while it is calling the success callback.  This
  // can happen if the device has a static IP configuration and releases
  // the lease after accepting other network parameters from the DHCP
  // IPConfig properties.  We need to ensure that no callbacks are left
  // running inadvertently as a result.
  EXPECT_CALL(*this, UpdateCallback(ConfigRef(), true))
      .WillOnce(InvokeWithoutArgs(this, &DHCPConfigTest::StopInstance));
  config_->ProcessEventSignal(DHCPv4Config::kReasonBound, conf);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(this));
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
}

TEST_F(DHCPConfigCallbackTest, NotifyUpdateWithDropRef) {
  // The UpdateCallback should be able to drop a reference to the
  // IPConfig object without crashing.
  EXPECT_CALL(*this, UpdateCallback(ConfigRef(), true))
      .WillOnce([this](const IPConfigRefPtr& /*ipconfig*/,
                       bool /*new_lease_acquired*/) {
        config_ = nullptr;
        ip_config_ = nullptr;
      });
  InvokeOnIPConfigUpdated({}, true);
}

TEST_F(DHCPConfigCallbackTest, ProcessAcquisitionTimeout) {
  // Do not fail on acquisition timeout (i.e. ARP gateway is active).
  SetShouldFailOnAcquisitionTimeout(false);
  EXPECT_CALL(*this, FailureCallback(_)).Times(0);
  config_->ProcessAcquisitionTimeout();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());

  // Fail on acquisition timeout.
  SetShouldFailOnAcquisitionTimeout(true);
  EXPECT_CALL(*this, FailureCallback(_)).Times(1);
  config_->ProcessAcquisitionTimeout();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());
}

TEST_F(DHCPConfigTest, ReleaseIP) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(1);
  SetShouldKeepLeaseOnDisconnect(false);
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect));
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, KeepLeaseOnDisconnect) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.

  // Keep lease on disconnect (i.e. ARP gateway is enabled).
  SetShouldKeepLeaseOnDisconnect(true);
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(0);
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect));
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, ReleaseLeaseOnDisconnect) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.

  // Release lease on disconnect.
  SetShouldKeepLeaseOnDisconnect(false);
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(1);
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect));
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, ReleaseIPStaticIPWithLease) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.
  config_->is_lease_active_ = true;
  EXPECT_CALL(*proxy_, Release(kDeviceName));
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonStaticIP));
  EXPECT_EQ(nullptr, config_->proxy_);
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, ReleaseIPStaticIPWithoutLease) {
  config_->pid_ = 1 << 18;  // Ensure unknown positive PID.
  config_->is_lease_active_ = false;
  EXPECT_CALL(*proxy_, Release(kDeviceName)).Times(0);
  MockDHCPProxy* proxy_pointer = proxy_.get();
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->ReleaseIP(IPConfig::kReleaseReasonStaticIP));
  // Expect that proxy has not been released.
  EXPECT_EQ(proxy_pointer, config_->proxy_.get());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, RenewIP) {
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(-1));
  config_->pid_ = 0;
  EXPECT_FALSE(config_->RenewIP());  // Expect a call to Start() if pid_ is 0.
  Mock::VerifyAndClearExpectations(&process_manager_);
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .Times(0);
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->lease_expiration_callback_.Reset(base::DoNothing());
  config_->pid_ = 456;
  EXPECT_FALSE(config_->RenewIP());  // Expect no crash with NULL proxy.
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->RenewIP());
  EXPECT_FALSE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, RequestIP) {
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->pid_ = 567;
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->RenewIP());
  EXPECT_FALSE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigCallbackTest, RequestIPTimeout) {
  SetShouldFailOnAcquisitionTimeout(true);
  EXPECT_CALL(*this, UpdateCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->lease_acquisition_timeout_ = base::TimeDelta();
  config_->pid_ = 567;
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_ = std::move(proxy_);
  config_->RenewIP();
  config_->dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, Restart) {
  const int kPID1 = 1 << 17;  // Ensure unknown positive PID.
  const int kPID2 = 987;
  config_->pid_ = kPID1;
  EXPECT_CALL(provider_, UnbindPID(kPID1));
  EXPECT_CALL(process_manager_, StopProcessAndBlock(kPID1))
      .WillOnce(Return(true));
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(kPID2));
  EXPECT_CALL(provider_, BindPID(kPID2, IsRefPtrTo(config_)));
  EXPECT_TRUE(config_->Restart());
  EXPECT_EQ(kPID2, config_->pid_);
  config_->pid_ = 0;
}

TEST_F(DHCPConfigTest, RestartNoClient) {
  const int kPID = 777;
  EXPECT_CALL(process_manager_, StopProcessAndBlock(_)).Times(0);
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(kPID));
  EXPECT_CALL(provider_, BindPID(kPID, IsRefPtrTo(config_)));
  EXPECT_TRUE(config_->Restart());
  EXPECT_EQ(kPID, config_->pid_);
  config_->pid_ = 0;
}

TEST_F(DHCPConfigCallbackTest, StartTimeout) {
  SetShouldFailOnAcquisitionTimeout(true);
  EXPECT_CALL(*this, UpdateCallback(_, _)).Times(0);
  EXPECT_CALL(*this, FailureCallback(ConfigRef()));
  config_->lease_acquisition_timeout_ = base::TimeDelta();
  config_->proxy_ = std::move(proxy_);
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(0));
  config_->Start();
  config_->dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(this);
  Mock::VerifyAndClearExpectations(config_.get());
}

TEST_F(DHCPConfigTest, Stop) {
  const int kPID = 1 << 17;  // Ensure unknown positive PID.
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(
      log,
      Log(_, _, ContainsRegex(base::StringPrintf("Stopping.+%s", __func__))));
  config_->pid_ = kPID;
  config_->lease_acquisition_timeout_callback_.Reset(base::DoNothing());
  config_->lease_expiration_callback_.Reset(base::DoNothing());
  EXPECT_CALL(provider_, UnbindPID(kPID));
  config_->Stop(__func__);
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  EXPECT_TRUE(config_->lease_expiration_callback_.IsCancelled());
  EXPECT_FALSE(config_->pid_);
}

TEST_F(DHCPConfigTest, StopDuringRequestIP) {
  config_->pid_ = 567;
  EXPECT_CALL(*proxy_, Rebind(kDeviceName)).Times(1);
  config_->proxy_ = std::move(proxy_);
  EXPECT_TRUE(config_->RenewIP());
  EXPECT_FALSE(config_->lease_acquisition_timeout_callback_.IsCancelled());
  config_->pid_ = 0;  // Keep Stop from killing a real process.
  config_->Stop(__func__);
  EXPECT_TRUE(config_->lease_acquisition_timeout_callback_.IsCancelled());
}

TEST_F(DHCPConfigTest, SetProperty) {
  Error error;
  std::string new_value = "new value";
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  config_->mutable_store()->SetAnyProperty(kAddressProperty,
                                           brillo::Any(new_value), &error);
  EXPECT_TRUE(error.IsFailure());
  EXPECT_EQ(Error::kInvalidArguments, error.type());
}

}  // namespace shill

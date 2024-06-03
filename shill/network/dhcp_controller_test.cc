// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_controller.h"

#include <memory>

#include <base/functional/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>

#include "shill/event_dispatcher.h"
#include "shill/mock_metrics.h"
#include "shill/mock_time.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/mock_dhcp_client_proxy.h"
#include "shill/technology.h"

namespace shill {
namespace {

constexpr char kDeviceName[] = "eth0";
constexpr Technology kTechnology = Technology::kEthernet;
const DHCPController::Options kOptions = {};
constexpr uint32_t kTimeNowInSeconds = 10;
constexpr base::TimeDelta kLeaseDuration = base::Minutes(30);

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

KeyValueStore GenerateConfiguration(
    std::optional<base::TimeDelta> lease_duration = std::nullopt) {
  KeyValueStore configuration;
  configuration.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress,
                              0x01020304);
  configuration.Set<uint8_t>(DHCPv4Config::kConfigurationKeySubnetCIDR, 16);
  configuration.Set<uint32_t>(DHCPv4Config::kConfigurationKeyBroadcastAddress,
                              0x10203040);
  if (lease_duration.has_value()) {
    configuration.Set<uint32_t>(DHCPv4Config::kConfigurationKeyLeaseTime,
                                lease_duration->InSeconds());
  }
  return configuration;
}

class DHCPControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    SetCurrentTimeToSecond(kTimeNowInSeconds);
    dhcp_controller_ = CreateDHCPControllerAndRenewIP(kOptions);
  }

  std::unique_ptr<DHCPController> CreateDHCPControllerAndRenewIP(
      const DHCPController::Options& options) {
    EXPECT_CALL(mock_dhcp_client_proxy_factory_,
                Create(kDeviceName, kTechnology, options, _))
        .WillOnce([this](std::string_view interface, Technology technology,
                         const DHCPController::Options& options,
                         DHCPClientProxy::EventHandler* handler) {
          auto dhcp_client_proxy = std::make_unique<MockDHCPClientProxy>(
              interface, handler,
              base::ScopedClosureRunner(base::BindOnce(
                  &DHCPControllerTest::OnDHCPClientProxyDestroyed,
                  base::Unretained(this))));
          mock_dhcp_client_proxy_ = dhcp_client_proxy.get();
          return dhcp_client_proxy;
        });

    auto controller = std::make_unique<DHCPController>(
        &dispatcher_, &metrics_, &mock_time_, &mock_dhcp_client_proxy_factory_,
        kDeviceName, kTechnology, options,
        base::BindRepeating(&DHCPControllerTest::UpdateCallback,
                            base::Unretained(this)),
        base::BindRepeating(&DHCPControllerTest::DropCallback,
                            base::Unretained(this)));
    EXPECT_TRUE(controller->RenewIP());

    testing::Mock::VerifyAndClearExpectations(&mock_dhcp_client_proxy_factory_);
    return controller;
  }

  // Creates a DHCPController that fails to initiates the DHCP client.
  std::unique_ptr<DHCPController> CreateDHCPControllerAndRenewIPFailed() {
    EXPECT_CALL(mock_dhcp_client_proxy_factory_, Create)
        .WillOnce(Return(nullptr));

    auto controller = std::make_unique<DHCPController>(
        &dispatcher_, &metrics_, &mock_time_, &mock_dhcp_client_proxy_factory_,
        kDeviceName, kTechnology, kOptions,
        base::BindRepeating(&DHCPControllerTest::UpdateCallback,
                            base::Unretained(this)),
        base::BindRepeating(&DHCPControllerTest::DropCallback,
                            base::Unretained(this)));
    EXPECT_FALSE(controller->RenewIP());

    testing::Mock::VerifyAndClearExpectations(&mock_dhcp_client_proxy_factory_);
    return controller;
  }

  // Sets the current time returned by time_.GetTimeBoottime() to |second|.
  void SetCurrentTimeToSecond(uint32_t second) {
    struct timeval current = {static_cast<__time_t>(second), 0};
    ON_CALL(mock_time_, GetTimeBoottime(_))
        .WillByDefault(DoAll(SetArgPointee<0>(current), Return(0)));
  }

  // Simulates the DHCPController receiving the DHCP lease.
  void ReceiveLeaseEvent() {
    const KeyValueStore configuration = GenerateConfiguration(kLeaseDuration);

    // The caller will receive the UpdateCallback with the NetworkConfig and
    // DHCPv4Config::Data parsed by DHCPv4Config::ParseConfiguration().
    net_base::NetworkConfig network_config;
    DHCPv4Config::Data dhcp_data;
    DHCPv4Config::ParseConfiguration(configuration, &network_config,
                                     &dhcp_data);
    EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                      /*new_lease_acquired=*/true));

    ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(true));
    dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                  configuration);
  }

  // The callback of the controller.
  MOCK_METHOD(void,
              UpdateCallback,
              (const net_base::NetworkConfig&,
               const DHCPv4Config::Data&,
               bool));
  MOCK_METHOD(void, DropCallback, (bool is_voluntary));
  MOCK_METHOD(void, OnDHCPClientProxyDestroyed, ());

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  EventDispatcher dispatcher_;
  MockMetrics metrics_;
  MockTime mock_time_;
  MockDHCPClientProxyFactory mock_dhcp_client_proxy_factory_;
  MockDHCPClientProxy* mock_dhcp_client_proxy_ = nullptr;

  std::unique_ptr<DHCPController> dhcp_controller_;
};

TEST_F(DHCPControllerTest, RenewIP) {
  ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(true));
  EXPECT_CALL(*mock_dhcp_client_proxy_, Rebind).WillOnce(Return(true));

  EXPECT_TRUE(dhcp_controller_->RenewIP());
}

TEST_F(DHCPControllerTest, RenewIPWhenDHCPClientNotReady) {
  ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(false));
  EXPECT_CALL(*mock_dhcp_client_proxy_, Rebind).Times(0);

  EXPECT_FALSE(dhcp_controller_->RenewIP());
}

TEST_F(DHCPControllerTest, RenewIPWhenDHCPClientNotRunning) {
  // The first DHCPClientProxy creation failed.
  dhcp_controller_ = CreateDHCPControllerAndRenewIPFailed();

  // Calling RenewIP() should create the DHCPClientProxy again.
  EXPECT_CALL(mock_dhcp_client_proxy_factory_,
              Create(kDeviceName, kTechnology, kOptions, _))
      .WillOnce([](std::string_view interface, Technology technology,
                   const DHCPController::Options& options,
                   DHCPClientProxy::EventHandler* handler) {
        return std::make_unique<MockDHCPClientProxy>(interface, handler);
      });
  EXPECT_TRUE(dhcp_controller_->RenewIP());
}

TEST_F(DHCPControllerTest, ReleaseIP) {
  ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(true));

  EXPECT_CALL(*mock_dhcp_client_proxy_, Release);
  // The DHCP client should be destroyed after releasing IP.
  EXPECT_CALL(*this, OnDHCPClientProxyDestroyed);

  EXPECT_TRUE(
      dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kDisconnect));
}

TEST_F(DHCPControllerTest, ReleaseIPWithGateARP) {
  const DHCPController::Options kOptionsUseARPGateway = {.use_arp_gateway =
                                                             true};

  dhcp_controller_ = CreateDHCPControllerAndRenewIP(kOptionsUseARPGateway);
  ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(true));

  // If we are using gateway unicast ARP to speed up re-connect, don't
  // give up our leases when we disconnect.
  EXPECT_CALL(*mock_dhcp_client_proxy_, Release).Times(0);

  EXPECT_TRUE(
      dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kDisconnect));
}

TEST_F(DHCPControllerTest, ReleaseIPWhenDHCPClientNotRunning) {
  // The first DHCPClientProxy creation failed.
  dhcp_controller_ = CreateDHCPControllerAndRenewIPFailed();

  // ReleaseIP() should be successful when no DHCP client is running.
  EXPECT_TRUE(
      dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kDisconnect));
}

TEST_F(DHCPControllerTest, ReleaseIPWhenLeaseNotActive) {
  // When the release reason is kStaticIP, ReleaseIP() should be successful when
  // the lease hasn't been retrieved yet.
  EXPECT_TRUE(
      dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kStaticIP));
}

TEST_F(DHCPControllerTest, OnProcessExited) {
  constexpr int kPid = 4;
  constexpr int kExitStatus = 0;

  // The DHCP client should be destroyed after the process is exited.
  EXPECT_CALL(*this, OnDHCPClientProxyDestroyed);
  dhcp_controller_->OnProcessExited(kPid, kExitStatus);
}

TEST_F(DHCPControllerTest, FailEvent) {
  EXPECT_CALL(*this, DropCallback(false));

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kFail, {});
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, IPv6OnlyPreferredEvent) {
  EXPECT_CALL(*this, DropCallback(true));

  dhcp_controller_->OnDHCPEvent(
      DHCPClientProxy::EventReason::kIPv6OnlyPreferred, {});
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, BoundEvent) {
  const KeyValueStore configuration = GenerateConfiguration();

  // The caller will receive the UpdateCallback with the NetworkConfig and
  // DHCPv4Config::Data parsed by DHCPv4Config::ParseConfiguration().
  net_base::NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  DHCPv4Config::ParseConfiguration(configuration, &network_config, &dhcp_data);
  EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                    /*new_lease_acquired=*/true));

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                configuration);
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, GatewayArpEvent) {
  // The caller will receive the UpdateCallback with the NetworkConfig and
  // DHCPv4Config::Data parsed by DHCPv4Config::ParseConfiguration().
  const KeyValueStore configuration = GenerateConfiguration();
  net_base::NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  DHCPv4Config::ParseConfiguration(configuration, &network_config, &dhcp_data);
  EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                    /*new_lease_acquired=*/false));

  // When the gateway ARP is active, it should not notify the caller failure
  // when the acquisition timeout.
  EXPECT_CALL(*this, DropCallback).Times(0);

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kGatewayArp,
                                configuration);

  task_environment_.FastForwardBy(DHCPController::kAcquisitionTimeout);
}

TEST_F(DHCPControllerTest, GatewayArpAndNakEvent) {
  // The caller will receive the UpdateCallback with the NetworkConfig and
  // DHCPv4Config::Data parsed by DHCPv4Config::ParseConfiguration().
  const KeyValueStore configuration = GenerateConfiguration();
  net_base::NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  DHCPv4Config::ParseConfiguration(configuration, &network_config, &dhcp_data);
  EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                    /*new_lease_acquired=*/false));

  // When the NAK event disable the gateway ARP, it should notify the caller
  // failure when the acquisition timeout.
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false)).Times(1);

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kGatewayArp,
                                configuration);
  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kNak, {});

  task_environment_.FastForwardBy(DHCPController::kAcquisitionTimeout);
}

TEST_F(DHCPControllerTest, GetAndResetLastProvisionDuration) {
  // Before receiving any event, the last provision duration should be null.
  EXPECT_EQ(dhcp_controller_->GetAndResetLastProvisionDuration(), std::nullopt);

  ReceiveLeaseEvent();
  task_environment_.RunUntilIdle();

  // After receiving any event, the last provision duration should not be null.
  EXPECT_NE(dhcp_controller_->GetAndResetLastProvisionDuration(), std::nullopt);
  // The value will be reset once GetAndResetLastProvisionDuration() is called.
  EXPECT_EQ(dhcp_controller_->GetAndResetLastProvisionDuration(), std::nullopt);
}

TEST_F(DHCPControllerTest, TimeToLeaseExpiry) {
  // TimeToLeaseExpiry() should return null when no DHCP lease is received.
  EXPECT_FALSE(dhcp_controller_->TimeToLeaseExpiry().has_value());

  // After DHCP lease is received, TimeToLeaseExpiry() should return the
  // remaining time before the lease is expired.
  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                GenerateConfiguration(kLeaseDuration));
  for (uint32_t i = 0; i <= kLeaseDuration.InSeconds(); i++) {
    SetCurrentTimeToSecond(kTimeNowInSeconds + i);
    EXPECT_EQ(dhcp_controller_->TimeToLeaseExpiry(),
              kLeaseDuration - base::Seconds(i));
  }

  // After the lease is expired, TimeToLeaseExpiry() should return null.
  SetCurrentTimeToSecond(kTimeNowInSeconds + kLeaseDuration.InSeconds() + 1);
  EXPECT_FALSE(dhcp_controller_->TimeToLeaseExpiry().has_value());
}

TEST_F(DHCPControllerTest, LeaseExpiry) {
  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                GenerateConfiguration(kLeaseDuration));

  // The expiry callback should be triggered right after the lease is expired.
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kMetricExpiredLeaseLengthSeconds,
                                  kTechnology, kLeaseDuration.InSeconds()));

  // When the lease is expired, a new DHCPClientProxy instance will be created.
  // If the creation fails, it should notify the caller.
  EXPECT_CALL(mock_dhcp_client_proxy_factory_,
              Create(kDeviceName, kTechnology, kOptions, _))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false));

  task_environment_.FastForwardBy(kLeaseDuration);
  testing::Mock::VerifyAndClearExpectations(this);
}

TEST_F(DHCPControllerTest, RenewIPCancelLeaseExpiration) {
  constexpr base::TimeDelta kPeriodBeforeExpiry = base::Seconds(1);

  ReceiveLeaseEvent();

  // RenewIP() should cancel the lease expiry timeout.
  task_environment_.FastForwardBy(kLeaseDuration - kPeriodBeforeExpiry);
  dhcp_controller_->RenewIP();

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kMetricExpiredLeaseLengthSeconds,
                                  kTechnology, _))
      .Times(0);
  task_environment_.FastForwardBy(kPeriodBeforeExpiry);
}

TEST_F(DHCPControllerTest, AcquisitionTimeout) {
  // When no lease is received and acquisition timeout, it should notify the
  // caller.
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false));

  task_environment_.FastForwardBy(DHCPController::kAcquisitionTimeout);
}

TEST_F(DHCPControllerTest, AcquisitionTimeoutCancelledByLease) {
  ReceiveLeaseEvent();

  // When the lease is received before acquisition timeout, it should not notify
  // the caller.
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false)).Times(0);
  task_environment_.FastForwardBy(DHCPController::kAcquisitionTimeout);

  testing::Mock::VerifyAndClearExpectations(this);
}

}  // namespace
}  // namespace shill

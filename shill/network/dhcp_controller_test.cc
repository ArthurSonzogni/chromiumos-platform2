// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_controller.h"

#include <memory>

#include <base/functional/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/network_config.h>

#include "shill/event_dispatcher.h"
#include "shill/metrics.h"
#include "shill/mock_metrics.h"
#include "shill/mock_time.h"
#include "shill/network/dhcp_provision_reasons.h"
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
constexpr DHCPProvisionReason kProvisionReason = DHCPProvisionReason::kConnect;

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

net_base::NetworkConfig GenerateNetworkConfig() {
  net_base::NetworkConfig network_config;
  network_config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.100/24");
  network_config.ipv4_broadcast =
      *net_base::IPv4Address::CreateFromString("192.168.1.1");
  return network_config;
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
                Create(kDeviceName, kTechnology, options, _, _,
                       net_base::IPFamily::kIPv4))
        .WillOnce([this](std::string_view interface, Technology technology,
                         const DHCPController::Options& options,
                         DHCPClientProxy::EventHandler* handler,
                         std::string_view, net_base::IPFamily) {
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
                            base::Unretained(this)),
        "mock_device mock_service sid=mock");
    EXPECT_TRUE(controller->RenewIP(kProvisionReason));

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
                            base::Unretained(this)),
        "mock_device mock_service sid=mock");
    EXPECT_FALSE(controller->RenewIP(kProvisionReason));

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
    const net_base::NetworkConfig network_config = GenerateNetworkConfig();
    const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};

    ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(true));

    EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                      /*new_lease_acquired=*/true));

    dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                  network_config, dhcp_data);
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

  EXPECT_TRUE(dhcp_controller_->RenewIP(kProvisionReason));
}

TEST_F(DHCPControllerTest, RenewIPWhenDHCPClientNotReady) {
  ON_CALL(*mock_dhcp_client_proxy_, IsReady).WillByDefault(Return(false));
  EXPECT_CALL(*mock_dhcp_client_proxy_, Rebind).Times(0);

  EXPECT_FALSE(dhcp_controller_->RenewIP(kProvisionReason));
}

TEST_F(DHCPControllerTest, RenewIPWhenDHCPClientNotRunning) {
  // The first DHCPClientProxy creation failed.
  dhcp_controller_ = CreateDHCPControllerAndRenewIPFailed();

  // Calling RenewIP() should create the DHCPClientProxy again.
  EXPECT_CALL(mock_dhcp_client_proxy_factory_,
              Create(kDeviceName, kTechnology, kOptions, _, _,
                     net_base::IPFamily::kIPv4))
      .WillOnce([](std::string_view interface, Technology technology,
                   const DHCPController::Options& options,
                   DHCPClientProxy::EventHandler* handler, std::string_view,
                   net_base::IPFamily) {
        return std::make_unique<MockDHCPClientProxy>(interface, handler);
      });
  EXPECT_TRUE(dhcp_controller_->RenewIP(kProvisionReason));
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
  EXPECT_CALL(metrics_, SendDHCPv4ProvisionResultEnumToUMA(
                            kTechnology, kProvisionReason,
                            Metrics::DHCPv4ProvisionResult::kClientFailure));
  EXPECT_CALL(*this, DropCallback(false));

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kFail, {}, {});
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, IPv6OnlyPreferredEvent) {
  EXPECT_CALL(metrics_,
              SendDHCPv4ProvisionResultEnumToUMA(
                  kTechnology, kProvisionReason,
                  Metrics::DHCPv4ProvisionResult::kIPv6OnlyPreferred));
  EXPECT_CALL(*this, DropCallback(true));

  dhcp_controller_->OnDHCPEvent(
      DHCPClientProxy::EventReason::kIPv6OnlyPreferred, {}, {});
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, BoundEvent) {
  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};

  EXPECT_CALL(metrics_, SendDHCPv4ProvisionResultEnumToUMA(
                            kTechnology, kProvisionReason,
                            Metrics::DHCPv4ProvisionResult::kSuccess));
  EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                    /*new_lease_acquired=*/true));

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                network_config, dhcp_data);
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, MultipleBoundEvents) {
  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};

  // At most one result can be emitted per provision.
  EXPECT_CALL(metrics_, SendDHCPv4ProvisionResultEnumToUMA(
                            kTechnology, kProvisionReason,
                            Metrics::DHCPv4ProvisionResult::kSuccess))
      .Times(1);

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                network_config, dhcp_data);
  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                network_config, dhcp_data);
  task_environment_.RunUntilIdle();
}

TEST_F(DHCPControllerTest, GatewayArpEvent) {
  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};
  EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                    /*new_lease_acquired=*/false));

  // When the gateway ARP is active, it should not notify the caller failure
  // when the acquisition timeout.
  EXPECT_CALL(*this, DropCallback).Times(0);

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kGatewayArp,
                                network_config, dhcp_data);

  task_environment_.FastForwardBy(DHCPController::kAcquisitionTimeout);
}

TEST_F(DHCPControllerTest, GatewayArpAndNakEvent) {
  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};
  EXPECT_CALL(*this, UpdateCallback(network_config, dhcp_data,
                                    /*new_lease_acquired=*/false));

  // When the NAK event disable the gateway ARP, it should notify the caller
  // failure when the acquisition timeout.
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false)).Times(1);

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kGatewayArp,
                                network_config, dhcp_data);
  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kNak, {}, {});

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

  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};
  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                network_config, dhcp_data);

  // After DHCP lease is received, TimeToLeaseExpiry() should return the
  // remaining time before the lease is expired.
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
  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                network_config, dhcp_data);

  // The expiry callback should be triggered right after the lease is expired.
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kMetricExpiredLeaseLengthSeconds,
                                  kTechnology, kLeaseDuration.InSeconds()));

  // When the lease is expired, a new DHCPClientProxy instance will be created.
  // If the creation fails, it should notify the caller.
  EXPECT_CALL(mock_dhcp_client_proxy_factory_,
              Create(kDeviceName, kTechnology, kOptions, _, _,
                     net_base::IPFamily::kIPv4))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false));

  task_environment_.FastForwardBy(kLeaseDuration);
  testing::Mock::VerifyAndClearExpectations(this);
}

TEST_F(DHCPControllerTest, LeaseExpiryProvisionReason) {
  const net_base::NetworkConfig network_config = GenerateNetworkConfig();
  const DHCPv4Config::Data dhcp_data{.lease_duration = kLeaseDuration};

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kBound,
                                network_config, dhcp_data);

  // When the lease is expired, the original proxy is removed. We need to create
  // a new proxy to restart provision.
  EXPECT_CALL(mock_dhcp_client_proxy_factory_,
              Create(kDeviceName, kTechnology, kOptions, _, _,
                     net_base::IPFamily::kIPv4))
      .WillOnce([](std::string_view interface, Technology,
                   const DHCPController::Options&,
                   DHCPClientProxy::EventHandler* handler, std::string_view,
                   net_base::IPFamily) {
        return std::make_unique<MockDHCPClientProxy>(interface, handler);
      });
  task_environment_.FastForwardBy(kLeaseDuration);

  // Now the provision should be triggered by lease expiration.
  EXPECT_EQ(DHCPProvisionReason::kLeaseExpiration,
            dhcp_controller_->provision_reason());
}

TEST_F(DHCPControllerTest, RenewIPCancelLeaseExpiration) {
  constexpr base::TimeDelta kPeriodBeforeExpiry = base::Seconds(1);

  ReceiveLeaseEvent();

  // RenewIP() should cancel the lease expiry timeout.
  task_environment_.FastForwardBy(kLeaseDuration - kPeriodBeforeExpiry);
  dhcp_controller_->RenewIP(kProvisionReason);

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kMetricExpiredLeaseLengthSeconds,
                                  kTechnology, _))
      .Times(0);
  task_environment_.FastForwardBy(kPeriodBeforeExpiry);
}

TEST_F(DHCPControllerTest, AcquisitionTimeout) {
  EXPECT_CALL(metrics_, SendDHCPv4ProvisionResultEnumToUMA(
                            kTechnology, kProvisionReason,
                            Metrics::DHCPv4ProvisionResult::kTimeout));
  // When no lease is received and acquisition timeout, it should notify the
  // caller.
  EXPECT_CALL(*this, DropCallback(/*is_voluntary=*/false));

  task_environment_.FastForwardBy(DHCPController::kAcquisitionTimeout);
}

TEST_F(DHCPControllerTest, AcquisitionTimeoutWithNak) {
  // If any NAK was received during provision, kNak instead of kTimeout should
  // be reported as the DHCPv4ProvisionResult.
  EXPECT_CALL(metrics_, SendDHCPv4ProvisionResultEnumToUMA(
                            kTechnology, kProvisionReason,
                            Metrics::DHCPv4ProvisionResult::kNak));
  EXPECT_CALL(metrics_, SendDHCPv4ProvisionResultEnumToUMA(
                            kTechnology, kProvisionReason,
                            Metrics::DHCPv4ProvisionResult::kTimeout))
      .Times(0);

  dhcp_controller_->OnDHCPEvent(DHCPClientProxy::EventReason::kNak, {}, {});
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

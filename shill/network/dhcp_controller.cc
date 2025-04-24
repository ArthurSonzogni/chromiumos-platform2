// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcp_controller.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/network_config.h>
#include <metrics/timer.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcp_provision_reasons.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/store/key_value_store.h"
#include "shill/technology.h"
#include "shill/time.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static std::string ObjectID(const DHCPController* d) {
  if (d == nullptr) {
    return "(dhcp_controller)";
  }
  return d->device_name();
}
}  // namespace Logging

DHCPController::DHCPController(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    Time* time,
    DHCPClientProxyFactory* dhcp_client_proxy_factory,
    std::string_view device_name,
    Technology technology,
    const Options& options,
    UpdateCallback update_callback,
    DropCallback drop_callback,
    std::string_view logging_tag,
    net_base::IPFamily family)
    : dispatcher_(dispatcher),
      metrics_(metrics),
      time_(time),
      device_name_(device_name),
      technology_(technology),
      family_(family),
      options_(options),
      update_callback_(std::move(update_callback)),
      drop_callback_(std::move(drop_callback)),
      use_arp_gateway_(options.use_arp_gateway),
      create_dhcp_client_proxy_cb_(
          base::BindRepeating(&DHCPClientProxyFactory::Create,
                              base::Unretained(dhcp_client_proxy_factory),
                              device_name,
                              technology,
                              options,
                              this,
                              logging_tag)),
      logging_tag_(logging_tag) {}

DHCPController::~DHCPController() = default;

bool DHCPController::RenewIP(DHCPProvisionReason reason) {
  SLOG(this, 2) << logging_tag_ << " " << __func__;
  UpdateProvisionStatus(reason);
  if (!dhcp_client_proxy_) {
    return Start();
  }

  if (!dhcp_client_proxy_->IsReady()) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": Unable to renew IP before acquiring destination.";
    ResetProvisionStatus();
    return false;
  }

  StopExpirationTimeout();
  dhcp_client_proxy_->Rebind();
  StartAcquisitionTimeout();
  return true;
}

bool DHCPController::ReleaseIP(ReleaseReason reason) {
  SLOG(this, 2) << logging_tag_ << " " << __func__;
  if (!dhcp_client_proxy_) {
    return true;
  }

  // If we are using static IP and haven't retrieved a lease yet, we should
  // allow the DHCP client to continue until we have a lease.
  if (!is_lease_active_ && reason == ReleaseReason::kStaticIP) {
    return true;
  }

  // If we are using gateway unicast ARP to speed up re-connect, don't
  // give up our leases when we disconnect.
  const bool should_keep_lease =
      reason == ReleaseReason::kDisconnect && use_arp_gateway_;
  if (!should_keep_lease && dhcp_client_proxy_->IsReady()) {
    dhcp_client_proxy_->Release();
  }

  Stop();
  return true;
}

void DHCPController::OnDHCPEvent(DHCPClientProxy::EventReason reason,
                                 const net_base::NetworkConfig& network_config,
                                 const DHCPv4Config::Data& dhcp_data) {
  switch (reason) {
    case DHCPClientProxy::EventReason::kFail:
      LOG(ERROR) << logging_tag_ << " " << __func__
                 << ": Received failure event from DHCP client.";
      SendDHCPv4ProvisionResultMetrics(
          Metrics::DHCPv4ProvisionResult::kClientFailure);
      NotifyDropCallback(false);
      return;

    case DHCPClientProxy::EventReason::kIPv6OnlyPreferred:
      SendDHCPv4ProvisionResultMetrics(
          Metrics::DHCPv4ProvisionResult::kIPv6OnlyPreferred);
      NotifyDropCallback(true);
      return;

    case DHCPClientProxy::EventReason::kNak:
      // If we got a NAK, this means the DHCP server is active, and any
      // Gateway ARP state we have is no longer sufficient.
      LOG_IF(ERROR, is_gateway_arp_active_)
          << logging_tag_ << " " << __func__
          << ": Received NAK event for our gateway-ARP lease.";
      nak_received_ = true;
      is_gateway_arp_active_ = false;
      return;

    case DHCPClientProxy::EventReason::kRenew:
      metrics_->SendEnumToUMA(Metrics::kMetricDHCPv4RenewRebind, technology_,
                              Metrics::DHCPv4RenewRebind::kRenew);
      SendDHCPv4ProvisionResultMetrics(
          Metrics::DHCPv4ProvisionResult::kSuccess);
      return;

    case DHCPClientProxy::EventReason::kRebind:
      metrics_->SendEnumToUMA(Metrics::kMetricDHCPv4RenewRebind, technology_,
                              Metrics::DHCPv4RenewRebind::kRebind);
      SendDHCPv4ProvisionResultMetrics(
          Metrics::DHCPv4ProvisionResult::kSuccess);
      return;

    case DHCPClientProxy::EventReason::kBound:
    case DHCPClientProxy::EventReason::kReboot:
      SendDHCPv4ProvisionResultMetrics(
          Metrics::DHCPv4ProvisionResult::kSuccess);
      [[fallthrough]];

    case DHCPClientProxy::EventReason::kBound6:
    case DHCPClientProxy::EventReason::kRebind6:
    case DHCPClientProxy::EventReason::kReboot6:
    case DHCPClientProxy::EventReason::kRenew6:
      UpdateConfiguration(network_config, dhcp_data, /*is_gateway_arp=*/false);
      return;

    case DHCPClientProxy::EventReason::kGatewayArp:
      UpdateConfiguration(network_config, dhcp_data, /*is_gateway_arp=*/true);
      return;
  }
}

void DHCPController::UpdateConfiguration(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& dhcp_data,
    bool is_gateway_arp) {
  // b/298696921#17: a race between GATEWAY-ARP response and DHCPACK can cause a
  // GATEWAY-ARP event incoming with no DHCP lease information. This empty lease
  // should be ignored.
  if (is_gateway_arp && !network_config.ipv4_address) {
    LOG(WARNING)
        << logging_tag_ << " " << __func__
        << ": Get GATEWAY-ARP reply before DHCP state change, ignored.";
    return;
  }

  // This needs to be set before calling OnIPConfigUpdated() below since
  // those functions may indirectly call other methods like ReleaseIP that
  // depend on or change this value.
  is_lease_active_ = true;

  // Only record the duration once. Note that Stop() has no effect if the timer
  // has already stopped.
  if (last_provision_timer_) {
    last_provision_timer_->Stop();
  }

  // This is a non-authoritative confirmation that we or on the same
  // network as the one we received a lease on previously.  The DHCP
  // client is still running, so we should not cancel the timeout
  // until that completes.  In the meantime, however, we can tentatively
  // configure our network in anticipation of successful completion.
  OnIPConfigUpdated(network_config, dhcp_data,
                    /*new_lease_acquired=*/!is_gateway_arp);
  is_gateway_arp_active_ = is_gateway_arp;
}

std::optional<base::TimeDelta> DHCPController::TimeToLeaseExpiry() {
  if (!current_lease_expiration_time_.has_value()) {
    SLOG(this, 2) << logging_tag_ << " " << __func__
                  << ": No current DHCP lease";
    return std::nullopt;
  }
  struct timeval now;
  time_->GetTimeBoottime(&now);
  if (now.tv_sec > current_lease_expiration_time_->tv_sec) {
    SLOG(this, 2) << logging_tag_ << " " << __func__
                  << ": Current DHCP lease has already expired";
    return std::nullopt;
  }
  return base::Seconds(current_lease_expiration_time_->tv_sec - now.tv_sec);
}

void DHCPController::OnIPConfigUpdated(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& dhcp_data,
    bool new_lease_acquired) {
  if (new_lease_acquired) {
    StopAcquisitionTimeout();
    if (dhcp_data.lease_duration.is_positive()) {
      UpdateLeaseExpirationTime(dhcp_data.lease_duration.InSeconds());
      StartExpirationTimeout(dhcp_data.lease_duration);
    } else {
      LOG(WARNING)
          << logging_tag_ << " " << __func__
          << ": Lease duration is zero; not starting an expiration timer.";
      ResetLeaseExpirationTime();
      StopExpirationTimeout();
    }
  }

  update_callback_.Run(network_config, dhcp_data, new_lease_acquired);
}

void DHCPController::NotifyDropCallback(bool is_voluntary) {
  StopAcquisitionTimeout();
  StopExpirationTimeout();

  drop_callback_.Run(is_voluntary);
}

bool DHCPController::Start() {
  SLOG(this, 2) << logging_tag_ << " " << __func__;

  if (dhcp_client_proxy_ != nullptr) {
    return true;
  }

  last_provision_timer_ = std::make_unique<chromeos_metrics::Timer>();
  last_provision_timer_->Start();

  dhcp_client_proxy_ = create_dhcp_client_proxy_cb_.Run(family_);
  if (dhcp_client_proxy_ == nullptr) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": Unable to create DHCP client proxy.";
    ResetProvisionStatus();
    return false;
  }

  StartAcquisitionTimeout();
  return true;
}

void DHCPController::Stop() {
  SLOG(this, 2) << logging_tag_ << " " << __func__;

  StopAcquisitionTimeout();
  StopExpirationTimeout();
  dhcp_client_proxy_.reset();

  is_lease_active_ = false;
  is_gateway_arp_active_ = false;
  ResetProvisionStatus();
}

void DHCPController::OnProcessExited(int pid, int exit_status) {
  SLOG(this, 2) << logging_tag_ << " " << __func__;
  Stop();
}

void DHCPController::StartAcquisitionTimeout() {
  CHECK(lease_expiration_callback_.IsCancelled());
  is_gateway_arp_active_ = false;
  lease_acquisition_timeout_callback_.Reset(
      base::BindOnce(&DHCPController::ProcessAcquisitionTimeout,
                     weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE,
                               lease_acquisition_timeout_callback_.callback(),
                               kAcquisitionTimeout);
}

void DHCPController::StopAcquisitionTimeout() {
  lease_acquisition_timeout_callback_.Cancel();
}

void DHCPController::ProcessAcquisitionTimeout() {
  LOG(ERROR) << logging_tag_ << " " << __func__
             << ": Timed out waiting for DHCP lease (after "
             << kAcquisitionTimeout.InSeconds() << " seconds).";

  // Send kNak if any NAK from the DHCP server was received during provision,
  // otherwise send kTimeout.
  SendDHCPv4ProvisionResultMetrics(
      nak_received_ ? Metrics::DHCPv4ProvisionResult::kNak
                    : Metrics::DHCPv4ProvisionResult::kTimeout);

  // Continue to use previous lease if gateway ARP is active.
  if (is_gateway_arp_active_) {
    LOG(INFO) << logging_tag_ << " " << __func__
              << ": Continuing to use our previous lease, due to gateway-ARP.";
  } else {
    NotifyDropCallback(false);
  }
}

void DHCPController::StartExpirationTimeout(base::TimeDelta lease_duration) {
  CHECK(lease_acquisition_timeout_callback_.IsCancelled());
  SLOG(this, 2) << logging_tag_ << " " << __func__ << ": Lease timeout is "
                << lease_duration.InSeconds() << " seconds.";
  lease_expiration_callback_.Reset(
      BindOnce(&DHCPController::ProcessExpirationTimeout,
               weak_ptr_factory_.GetWeakPtr(), lease_duration));
  dispatcher_->PostDelayedTask(FROM_HERE, lease_expiration_callback_.callback(),
                               lease_duration);
}

void DHCPController::StopExpirationTimeout() {
  lease_expiration_callback_.Cancel();
}

void DHCPController::ProcessExpirationTimeout(base::TimeDelta lease_duration) {
  LOG(ERROR) << logging_tag_ << " " << __func__
             << ": DHCP lease expired, restarting DHCP client instance.";

  metrics_->SendToUMA(Metrics::kMetricExpiredLeaseLengthSeconds, technology_,
                      lease_duration.InSeconds());

  Stop();
  UpdateProvisionStatus(DHCPProvisionReason::kLeaseExpiration);
  if (!Start()) {
    NotifyDropCallback(false);
  }
}

void DHCPController::UpdateLeaseExpirationTime(uint32_t new_lease_duration) {
  struct timeval new_expiration_time;
  time_->GetTimeBoottime(&new_expiration_time);
  new_expiration_time.tv_sec += new_lease_duration;
  current_lease_expiration_time_ = new_expiration_time;
}

void DHCPController::ResetLeaseExpirationTime() {
  current_lease_expiration_time_ = std::nullopt;
}

void DHCPController::UpdateProvisionStatus(DHCPProvisionReason reason) {
  provision_reason_ = reason;
  nak_received_ = false;
}

void DHCPController::ResetProvisionStatus() {
  provision_reason_.reset();
  nak_received_ = false;
}

void DHCPController::SendDHCPv4ProvisionResultMetrics(
    Metrics::DHCPv4ProvisionResult result) {
  // Only send DHCPv4 result.
  if (family_ == net_base::IPFamily::kIPv6) {
    return;
  }

  if (!provision_reason_) {
    return;
  }

  metrics_->SendDHCPv4ProvisionResultEnumToUMA(technology_, *provision_reason_,
                                               result);

  // Reset the provision status so that we won't report a result again for
  // current provision.
  ResetProvisionStatus();
}

std::optional<base::TimeDelta>
DHCPController::GetAndResetLastProvisionDuration() {
  if (!last_provision_timer_) {
    return std::nullopt;
  }

  if (last_provision_timer_->HasStarted()) {
    // The timer is still running, which means we haven't got any address.
    return std::nullopt;
  }

  base::TimeDelta ret;
  if (!last_provision_timer_->GetElapsedTime(&ret)) {
    // The timer has not been started. This shouldn't happen since Start() is
    // called right after the timer is created.
    return std::nullopt;
  }

  last_provision_timer_.reset();
  return ret;
}

DHCPControllerFactory::DHCPControllerFactory(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    Time* time,
    DHCPClientProxyFactory* dhcp_client_proxy_factory)
    : dispatcher_(dispatcher),
      metrics_(metrics),
      time_(time),
      dhcp_client_proxy_factory_(dhcp_client_proxy_factory) {}

DHCPControllerFactory::~DHCPControllerFactory() = default;

std::unique_ptr<DHCPController> DHCPControllerFactory::Create(
    std::string_view device_name,
    Technology technology,
    const DHCPController::Options& options,
    DHCPController::UpdateCallback update_callback,
    DHCPController::DropCallback drop_callback,
    std::string_view logging_tag,
    net_base::IPFamily family) {
  return std::make_unique<DHCPController>(
      dispatcher_, metrics_, time_, dhcp_client_proxy_factory_, device_name,
      technology, options, std::move(update_callback), std::move(drop_callback),
      logging_tag, family);
}

}  // namespace shill

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_monitor.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/http/http_transport.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "shill/event_dispatcher.h"
#include "shill/network/capport_proxy.h"
#include "shill/network/connection_diagnostics.h"
#include "shill/network/portal_detector.h"
#include "shill/network/trial_scheduler.h"
#include "shill/network/validation_log.h"

namespace shill {
namespace {

// Returns true if |reason| requires that the next network validation attempt
// be scheduled immediately.
bool ShouldScheduleNetworkValidationImmediately(
    NetworkMonitor::ValidationReason reason) {
  switch (reason) {
    case NetworkMonitor::ValidationReason::kDBusRequest:
    case NetworkMonitor::ValidationReason::kEthernetGatewayReachable:
    case NetworkMonitor::ValidationReason::kServiceReorder:
    case NetworkMonitor::ValidationReason::kCapportTimeOver:
      return true;
    case NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable:
    case NetworkMonitor::ValidationReason::kManagerPropertyUpdate:
    case NetworkMonitor::ValidationReason::kNetworkConnectionUpdate:
    case NetworkMonitor::ValidationReason::kRetryValidation:
    case NetworkMonitor::ValidationReason::kServicePropertyUpdate:
    case NetworkMonitor::ValidationReason::kCapportEnabled:
      return false;
  }
}

std::optional<net_base::IPFamily> GetNetworkValidationIPFamily(
    const net_base::NetworkConfig& network_config) {
  if (network_config.ipv4_address) {
    return net_base::IPFamily::kIPv4;
  }
  if (!network_config.ipv6_addresses.empty()) {
    return net_base::IPFamily::kIPv6;
  }
  return std::nullopt;
}

std::vector<net_base::IPAddress> GetNetworkValidationDNSServers(
    const net_base::NetworkConfig& network_config, net_base::IPFamily family) {
  std::vector<net_base::IPAddress> dns_list;
  for (const auto& addr : network_config.dns_servers) {
    if (addr.GetFamily() == family) {
      dns_list.push_back(addr);
    }
  }
  return dns_list;
}

}  // namespace

NetworkMonitor::NetworkMonitor(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    ClientNetwork* client,
    patchpanel::Client* patchpanel_client,
    Technology technology,
    int interface_index,
    std::string_view interface,
    PortalDetector::ProbingConfiguration probing_configuration,
    NetworkMonitor::ValidationMode validation_mode,
    std::unique_ptr<ValidationLog> network_validation_log,
    std::string_view logging_tag,
    std::unique_ptr<CapportProxyFactory> capport_proxy_factory,
    std::unique_ptr<ConnectionDiagnosticsFactory>
        connection_diagnostics_factory)
    : dispatcher_(dispatcher),
      patchpanel_client_(patchpanel_client),
      metrics_(metrics),
      client_(client),
      technology_(technology),
      interface_index_(interface_index),
      interface_(std::string(interface)),
      logging_tag_(std::string(logging_tag)),
      probing_configuration_(probing_configuration),
      validation_mode_(validation_mode),
      trial_scheduler_(dispatcher),
      capport_proxy_factory_(std::move(capport_proxy_factory)),
      validation_log_(std::move(network_validation_log)),
      connection_diagnostics_factory_(
          std::move(connection_diagnostics_factory)) {
  portal_detector_ = std::make_unique<PortalDetector>(
      dispatcher_, patchpanel_client_, interface_, probing_configuration_,
      logging_tag_);
}

NetworkMonitor::~NetworkMonitor() {
  StopNetworkValidationLog();
}

void NetworkMonitor::Start(ValidationReason reason) {
  // If the validation reason requires an immediate restart, reset the interval
  // scheduled between attempts.
  if (ShouldScheduleNetworkValidationImmediately(reason)) {
    trial_scheduler_.ResetInterval();
  }
  // Cancel the pending trial if exists.
  if (trial_scheduler_.IsTrialScheduled()) {
    trial_scheduler_.CancelTrial();
  }

  // base::Unretained() is safe because |trial_scheduler_| is owned by |*this|.
  // When the task is executed, |*this| is guaranteed alive.
  trial_scheduler_.ScheduleTrial(
      base::BindOnce(&NetworkMonitor::StartValidationTask,
                     base::Unretained(this), reason)
          .Then(base::BindOnce(&ClientNetwork::OnValidationStarted,
                               base::Unretained(client_))));
}

bool NetworkMonitor::StartValidationTask(ValidationReason reason) {
  const net_base::NetworkConfig& config = client_->GetCurrentConfig();
  const std::optional<net_base::IPFamily> ip_family =
      GetNetworkValidationIPFamily(config);
  if (!ip_family) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << "(" << reason
               << "): Cannot start portal detection: No valid IP address";
    return false;
  }
  const std::vector<net_base::IPAddress> dns_list =
      GetNetworkValidationDNSServers(config, *ip_family);
  if (dns_list.empty()) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << "(" << reason
               << "): Cannot start portal detection: No DNS servers";
    return false;
  }

  result_from_portal_detector_.reset();
  bool http_only = validation_mode_ == ValidationMode::kHTTPOnly;
  portal_detector_->Start(
      http_only, *ip_family, dns_list,
      base::BindOnce(&NetworkMonitor::OnPortalDetectorResult,
                     base::Unretained(this)));
  LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
            << "): Portal detection started.";

  if (capport_proxy_) {
    if (!capport_enabled_) {
      LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
                << "): CAPPORT is disabled, skip querying CAPPORT API.";
    } else {
      result_from_capport_proxy_.reset();
      if (capport_proxy_->IsRunning()) {
        capport_proxy_->Stop();
      }
      capport_proxy_->SendRequest(base::BindOnce(
          &NetworkMonitor::OnCapportStatusReceived, base::Unretained(this)));
      LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
                << "): Query CAPPORT API.";
    }
  }
  return true;
}

bool NetworkMonitor::Stop() {
  const bool was_running = IsRunning();
  portal_detector_->Reset();
  if (capport_proxy_) {
    capport_proxy_->Stop();
  }
  return was_running;
}

bool NetworkMonitor::IsRunning() const {
  return portal_detector_->IsRunning() ||
         (capport_proxy_ && capport_proxy_->IsRunning());
}

void NetworkMonitor::SetCapportURL(
    const net_base::HttpUrl& capport_url,
    base::span<const net_base::IPAddress> dns_list,
    CapportSource source) {
  if (validation_log_) {
    switch (source) {
      case CapportSource::kDHCP:
        validation_log_->SetCapportDHCPSupported();
        break;
      case CapportSource::kRA:
        validation_log_->SetCapportRASupported();
        break;
    }
  }

  if (!capport_proxy_) {
    capport_proxy_ = capport_proxy_factory_->Create(
        metrics_, patchpanel_client_, interface_, capport_url, dns_list);
  }
}

void NetworkMonitor::SetTermsAndConditions(const net_base::HttpUrl& url) {
  validation_log_->SetHasTermsAndConditions();
}

void NetworkMonitor::OnPortalDetectorResult(
    const PortalDetector::Result& result) {
  if (validation_log_) {
    validation_log_->AddPortalDetectorResult(result);
  }

  const int64_t total_duration =
      std::max(result.http_duration.InMilliseconds(),
               result.https_duration.InMilliseconds());
  switch (result.GetValidationState()) {
    case PortalDetector::ValidationState::kNoConnectivity:
      // If network validation cannot verify Internet access, then start
      // additional connection diagnostics for the current network connection.
      StartConnectionDiagnostics();
      break;
    case PortalDetector::ValidationState::kInternetConnectivity:
      metrics_->SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                          technology_, total_duration);
      // Stop recording results in |network_validation_log_| as soon as the
      // first kInternetConnectivity result is observed.
      StopNetworkValidationLog();
      break;
    case PortalDetector::ValidationState::kPortalRedirect:
      metrics_->SendToUMA(Metrics::kPortalDetectorPortalDiscoveryDuration,
                          technology_, total_duration);
      break;
    case PortalDetector::ValidationState::kPortalSuspected:
      break;
  }
  if (result.http_duration.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration, technology_,
                        result.http_duration.InMilliseconds());
  }
  if (result.https_duration.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration, technology_,
                        result.https_duration.InMilliseconds());
  }
  if (const auto http_response_code =
          result.GetHTTPResponseCodeMetricResult()) {
    metrics_->SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              technology_, *http_response_code);
  }
  if (result.http_status_code == brillo::http::status_code::Ok &&
      result.http_content_length) {
    metrics_->SendToUMA(Metrics::kPortalDetectorHTTPResponseContentLength,
                        technology_, *result.http_content_length);
  }

  result_from_portal_detector_ = Result::FromPortalDetectorResult(result);
  if (ShouldSendNewResult(result_from_portal_detector_,
                          result_from_capport_proxy_)) {
    client_->OnNetworkMonitorResult(*result_from_portal_detector_);
  }
}

void NetworkMonitor::OnCapportStatusReceived(
    const std::optional<CapportStatus>& status) {
  if (!status.has_value()) {
    return;
  }

  if (validation_log_) {
    validation_log_->AddCAPPORTStatus(*status);
  }

  if (status->seconds_remaining.has_value()) {
    // Cancel the previous posted task if exists.
    weak_ptr_factory_for_capport_.InvalidateWeakPtrs();
    dispatcher_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&NetworkMonitor::Start,
                       weak_ptr_factory_for_capport_.GetWeakPtr(),
                       ValidationReason::kCapportTimeOver),
        *status->seconds_remaining + kCapportRemainingExtraDelay);
  }

  // Use the attempt count from |portal_detector_| to keep the count of the
  // results from both side the same.
  const int num_attempts = portal_detector_->attempt_count();
  result_from_capport_proxy_ = Result::FromCapportStatus(*status, num_attempts);
  if (ShouldSendNewResult(result_from_capport_proxy_,
                          result_from_portal_detector_)) {
    client_->OnNetworkMonitorResult(*result_from_capport_proxy_);
  }
}

bool NetworkMonitor::ShouldSendNewResult(
    const std::optional<Result>& new_result,
    const std::optional<Result>& other_result) const {
  if (!new_result.has_value()) {
    return false;
  }

  switch (new_result->origin) {
    case ResultOrigin::kCapport:
      // We always trust the result from CAPPORT.
      return true;
    case ResultOrigin::kProbe:
      // If CAPPORT already replies the result, then we skip the result from
      // legacy probe.
      return !other_result.has_value();
  }
}

void NetworkMonitor::StopNetworkValidationLog() {
  if (validation_log_) {
    validation_log_->RecordMetrics();
    validation_log_.reset();
  }
}

void NetworkMonitor::StartConnectionDiagnostics() {
  const net_base::NetworkConfig& config = client_->GetCurrentConfig();

  std::optional<net_base::IPAddress> local_address = std::nullopt;
  std::optional<net_base::IPAddress> gateway_address = std::nullopt;
  if (config.ipv4_address) {
    local_address = net_base::IPAddress(config.ipv4_address->address());
    gateway_address =
        config.ipv4_gateway
            ? std::make_optional(net_base::IPAddress(*config.ipv4_gateway))
            : std::nullopt;
  } else if (!config.ipv6_addresses.empty()) {
    local_address = net_base::IPAddress(config.ipv6_addresses[0].address());
    gateway_address =
        config.ipv6_gateway
            ? std::make_optional(net_base::IPAddress(*config.ipv6_gateway))
            : std::nullopt;
  }

  if (!local_address) {
    LOG(ERROR)
        << logging_tag_ << " " << __func__
        << ": Local address unavailable, aborting connection diagnostics";
    return;
  }
  if (!gateway_address) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": Gateway unavailable, aborting connection diagnostics";
    return;
  }

  connection_diagnostics_ = connection_diagnostics_factory_->Create(
      interface_, interface_index_, *local_address, *gateway_address,
      config.dns_servers, dispatcher_);
  if (!connection_diagnostics_->Start(probing_configuration_.portal_http_url)) {
    connection_diagnostics_.reset();
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": Failed to start connection diagnostics";
    return;
  }
  LOG(INFO) << logging_tag_ << " " << __func__
            << ": Connection diagnostics started";
}

void NetworkMonitor::SetValidationMode(
    NetworkMonitor::ValidationMode validation_mode) {
  if (validation_mode_ == validation_mode) {
    return;
  }
  LOG(INFO) << logging_tag_ << " " << __func__ << ": " << validation_mode_
            << " -> " << validation_mode;
  // TODO(b/314693271): Stop or restart network validation if needed.
  validation_mode_ = validation_mode;
}

void NetworkMonitor::SetCapportEnabled(bool enabled) {
  if (capport_enabled_ == enabled) {
    return;
  }

  capport_enabled_ = enabled;
  if (capport_enabled_ && capport_proxy_) {
    LOG(INFO) << logging_tag_ << " " << __func__
              << ": Restart validation for CAPPORT enabled";
    Start(NetworkMonitor::ValidationReason::kCapportEnabled);
  }
}

void NetworkMonitor::set_portal_detector_for_testing(
    std::unique_ptr<PortalDetector> portal_detector) {
  portal_detector_ = std::move(portal_detector);
}

void NetworkMonitor::set_capport_proxy_for_testing(
    std::unique_ptr<CapportProxy> capport_proxy) {
  capport_proxy_ = std::move(capport_proxy);
}

void NetworkMonitor::OnPortalDetectorResultForTesting(
    const PortalDetector::Result& result) {
  OnPortalDetectorResult(result);
}

void NetworkMonitor::OnCapportStatusReceivedForTesting(
    const std::optional<CapportStatus>& status) {
  OnCapportStatusReceived(status);
}

std::unique_ptr<NetworkMonitor> NetworkMonitorFactory::Create(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    NetworkMonitor::ClientNetwork* client,
    patchpanel::Client* patchpanel_client,
    Technology technology,
    int interface_index,
    std::string_view interface,
    PortalDetector::ProbingConfiguration probing_configuration,
    NetworkMonitor::ValidationMode validation_mode,
    std::unique_ptr<ValidationLog> network_validation_log,
    std::string_view logging_tag) {
  return std::make_unique<NetworkMonitor>(
      dispatcher, metrics, client, patchpanel_client, technology,
      interface_index, interface, probing_configuration, validation_mode,
      std::move(network_validation_log), logging_tag);
}

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationMode mode) {
  switch (mode) {
    case NetworkMonitor::ValidationMode::kDisabled:
      return stream << "Disabled";
    case NetworkMonitor::ValidationMode::kFullValidation:
      return stream << "FullValidation";
    case NetworkMonitor::ValidationMode::kHTTPOnly:
      return stream << "HTTPOnly";
  }
}

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationReason reason) {
  switch (reason) {
    case NetworkMonitor::ValidationReason::kNetworkConnectionUpdate:
      return stream << "NetworkConnectionUpdate";
    case NetworkMonitor::ValidationReason::kServiceReorder:
      return stream << "ServiceReorder";
    case NetworkMonitor::ValidationReason::kServicePropertyUpdate:
      return stream << "ServicePropertyUpdate";
    case NetworkMonitor::ValidationReason::kManagerPropertyUpdate:
      return stream << "ManagerPropertyUpdate";
    case NetworkMonitor::ValidationReason::kDBusRequest:
      return stream << "DbusRequest";
    case NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable:
      return stream << "EthernetGatewayUnreachable";
    case NetworkMonitor::ValidationReason::kEthernetGatewayReachable:
      return stream << "EthernetGatewayReachable";
    case NetworkMonitor::ValidationReason::kRetryValidation:
      return stream << "RetryValidation";
    case NetworkMonitor::ValidationReason::kCapportTimeOver:
      return stream << "CapportTimeOver";
    case NetworkMonitor::ValidationReason::kCapportEnabled:
      return stream << "CapportEnabled";
  }
}

NetworkMonitor::Result NetworkMonitor::Result::FromPortalDetectorResult(
    const PortalDetector::Result& result) {
  return Result{
      .origin = ResultOrigin::kProbe,
      .num_attempts = result.num_attempts,
      .validation_state = result.GetValidationState(),
      .probe_result_metric = result.GetResultMetric(),
      .target_url = result.probe_url,
  };
}

NetworkMonitor::Result NetworkMonitor::Result::FromCapportStatus(
    const CapportStatus& status, int num_attempts) {
  if (!status.is_captive) {
    // RFC8908 does not allow the client to distinguish between a local-only
    // network without Internet and a netwoork with Internet access. So for now
    // we assume that a CAPPORT network where is_captive is false is considered
    // as kInternetConnectivity, but this may not be true all the time (e.g
    // in-flight entertainment WiFi without satellite Internet).
    return Result{
        .origin = ResultOrigin::kCapport,
        .num_attempts = num_attempts,
        .validation_state =
            PortalDetector::ValidationState::kInternetConnectivity,
        .probe_result_metric = Metrics::kPortalDetectorResultOnline,
    };
  }

  CHECK(status.user_portal_url.has_value());
  return Result{
      .origin = ResultOrigin::kCapport,
      .num_attempts = num_attempts,
      .validation_state = PortalDetector::ValidationState::kPortalRedirect,
      // TODO(b/305129516): Create a dedicated enum item for this case.
      .probe_result_metric = Metrics::kPortalDetectorResultRedirectFound,
      .target_url = status.user_portal_url,
  };
}

bool NetworkMonitor::Result::operator==(
    const NetworkMonitor::Result& rhs) const = default;

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ResultOrigin result_origin) {
  switch (result_origin) {
    case NetworkMonitor::ResultOrigin::kProbe:
      stream << "HTTP probe";
      break;
    case NetworkMonitor::ResultOrigin::kCapport:
      stream << "CAPPORT";
      break;
  }
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         const NetworkMonitor::Result& result) {
  stream << "{ origin=" << result.origin;
  stream << ", num_attempts=" << result.num_attempts;
  stream << ", validation_state=" << result.validation_state;
  stream << ", result_metric=" << result.probe_result_metric;
  if (result.target_url) {
    stream << ", target_url=" << result.target_url->ToString();
  }
  return stream << " }";
}

}  // namespace shill

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_monitor.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>

#include "shill/network/validation_log.h"

namespace shill {
namespace {

// Returns true if |reason| requires that network validation be entirely
// restarted with the latest IP configuration settings.
bool ShouldResetNetworkValidation(NetworkMonitor::ValidationReason reason) {
  // Only reset PortalDetector if there was an IP provisioning event.
  return reason == NetworkMonitor::ValidationReason::kNetworkConnectionUpdate;
}

// Returns true if |reason| requires that the next network validation attempt
// be scheduled immediately.
bool ShouldScheduleNetworkValidationImmediately(
    NetworkMonitor::ValidationReason reason) {
  switch (reason) {
    case NetworkMonitor::ValidationReason::kDBusRequest:
    case NetworkMonitor::ValidationReason::kEthernetGatewayReachable:
    case NetworkMonitor::ValidationReason::kNetworkConnectionUpdate:
    case NetworkMonitor::ValidationReason::kServiceReorder:
      return true;
    case NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable:
    case NetworkMonitor::ValidationReason::kManagerPropertyUpdate:
    case NetworkMonitor::ValidationReason::kServicePropertyUpdate:
    case NetworkMonitor::ValidationReason::kRetryValidation:
      return false;
  }
}

}  // namespace

NetworkMonitor::NetworkMonitor(
    EventDispatcher* dispatcher,
    std::string_view interface,
    PortalDetector::ProbingConfiguration probing_configuration,
    ResultCallback result_callback,
    std::unique_ptr<ValidationLog> network_validation_log,
    std::string_view logging_tag,
    std::unique_ptr<PortalDetectorFactory> portal_detector_factory)
    : dispatcher_(dispatcher),
      interface_(std::string(interface)),
      logging_tag_(std::string(logging_tag)),
      probing_configuration_(probing_configuration),
      result_callback_(std::move(result_callback)),
      portal_detector_factory_(std::move(portal_detector_factory)),
      validation_log_(std::move(network_validation_log)) {}

NetworkMonitor::~NetworkMonitor() {
  StopNetworkValidationLog();
}

bool NetworkMonitor::Start(ValidationReason reason,
                           net_base::IPFamily ip_family,
                           const std::vector<net_base::IPAddress>& dns_list) {
  if (dns_list.empty()) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << "(" << reason
               << "): Cannot start portal detection: No DNS servers";
    portal_detector_.reset();
    return false;
  }

  // Create a new PortalDetector instance and start the first trial if portal
  // detection:
  //   - has not been initialized yet,
  //   - or has stopped,
  //   - or should be reset immediately entirely.
  if (!portal_detector_ || ShouldResetNetworkValidation(reason)) {
    portal_detector_ = portal_detector_factory_->Create(
        dispatcher_, probing_configuration_,
        base::BindRepeating(&NetworkMonitor::OnPortalDetectorResult,
                            weak_ptr_factory_.GetWeakPtr()));
  }

  // Otherwise, if the validation reason requires an immediate restart, reset
  // the delay scheduled between attempts.
  if (ShouldScheduleNetworkValidationImmediately(reason)) {
    portal_detector_->ResetAttemptDelays();
  }

  // If portal detection is not running, reschedule the next a trial.
  if (portal_detector_->IsInProgress()) {
    LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
              << "): Portal detection is already running.";
    return true;
  }

  portal_detector_->Start(interface_, ip_family, dns_list, logging_tag_);
  LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
            << "): Portal detection started.";
  return true;
}

bool NetworkMonitor::Stop() {
  if (!portal_detector_) {
    return false;
  }
  portal_detector_.reset();
  return true;
}

void NetworkMonitor::SetCapportAPI(const net_base::HttpUrl& capport_api,
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

  // TODO(b/305129516): Initiate CapportClient.
}

void NetworkMonitor::OnPortalDetectorResult(
    const PortalDetector::Result& result) {
  if (validation_log_) {
    validation_log_->AddResult(result);
  }

  // Stop recording results in |network_validation_log_| as soon as the
  // first kInternetConnectivity result is observed.
  if (result.GetValidationState() ==
      PortalDetector::ValidationState::kInternetConnectivity) {
    StopNetworkValidationLog();
  }

  result_callback_.Run(result);
}

void NetworkMonitor::StopNetworkValidationLog() {
  if (validation_log_) {
    validation_log_->RecordMetrics();
    validation_log_.reset();
  }
}

void NetworkMonitor::set_portal_detector_for_testing(
    std::unique_ptr<PortalDetector> portal_detector) {
  portal_detector_ = std::move(portal_detector);
}

std::unique_ptr<NetworkMonitor> NetworkMonitorFactory::Create(
    EventDispatcher* dispatcher,
    std::string_view interface,
    PortalDetector::ProbingConfiguration probing_configuration,
    NetworkMonitor::ResultCallback result_callback,
    std::unique_ptr<ValidationLog> network_validation_log,
    std::string_view logging_tag) {
  return std::make_unique<NetworkMonitor>(
      dispatcher, interface, probing_configuration, std::move(result_callback),
      std::move(network_validation_log), logging_tag);
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
  }
}

}  // namespace shill

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_MONITOR_H_
#define SHILL_NETWORK_NETWORK_MONITOR_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <net-base/http_url.h>
#include <net-base/network_config.h>

#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/network/capport_proxy.h"
#include "shill/network/connection_diagnostics.h"
#include "shill/network/trial_scheduler.h"
#include "shill/portal_detector.h"
#include "shill/technology.h"

namespace shill {

// Forward declaration to avoid circular inclusion.
class ValidationLog;

// The NetworkMonitor class monitors the general Internet connectivity and the
// existence of the captive portal by triggering the PortalDetector and
// CapportClient. Also, the class sends the network validation metrics.
// TODO(b/305129516): Integrate the CapportClient into this class.
class NetworkMonitor {
 public:
  // Indicates the type of network validation to conduct on a connected Network.
  // TODO(b/318370676): Add "HTTP probe only" mode.
  enum class ValidationMode {
    // Network validation with web probes is disabled. Captive portal detection
    // with CAPPORT or Passpoint R3 can still occur.
    kDisabled,
    // Network validation with web probes is enabled. Both HTTPS validation and
    // HTTP captive portal detection are performed.
    kFullValidation,
  };

  // Reasons for starting portal validation on a Network.
  enum class ValidationReason {
    // IPv4 or IPv6 configuration of the network has completed.
    kNetworkConnectionUpdate,
    // Service order has changed.
    kServiceReorder,
    // A Service property relevant to network validation has changed.
    kServicePropertyUpdate,
    // A Manager property relevant to network validation has changed.
    kManagerPropertyUpdate,
    // A DBus request to recheck network validation has been received.
    kDBusRequest,
    // A L2 neighbor event has been received for an ethernet link indicating
    // the gateway is not reachable. This event can trigger Internet access
    // revalidation checks only on ethernet links.
    kEthernetGatewayUnreachable,
    // A L2 neighbor event has been received for an ethernet link indicating
    // the gateway is reachable. This event can trigger Internet access
    // revalidation checks only on ethernet links.
    kEthernetGatewayReachable,
    // Retry the validation when the previous one fails.
    kRetryValidation,
  };

  // Indicates the source of the CAPPORT API.
  enum class CapportSource {
    kDHCP,
    kRA,
  };

  // Indicates where the result comes from.
  enum class ResultOrigin {
    kProbe,    // From PortalDetector.
    kCapport,  // From CapportProxy.
  };

  // Represents the detailed result of a complete network validation attempt.
  struct Result {
    ResultOrigin origin;

    // The total number of trial attempts so far.
    int num_attempts = 0;

    // The outcome of the network validation.
    PortalDetector::ValidationState validation_state =
        PortalDetector::ValidationState::kNoConnectivity;

    // The metrics enum of the probe result.
    Metrics::PortalDetectorResult probe_result_metric =
        Metrics::kPortalDetectorResultUnknown;

    // Target URL when |validation_state| is either kPortalRedirect or
    // kPortalSuspected.
    std::optional<net_base::HttpUrl> target_url = std::nullopt;

    static Result FromPortalDetectorResult(
        const PortalDetector::Result& result);
    static Result FromCapportStatus(const CapportStatus& status,
                                    int num_attempts);

    bool operator==(const Result& rhs) const;
  };

  // This interface defines the interactions between the NetworkMonitor and its
  // caller.
  class ClientNetwork {
   public:
    // Gets the current network configuration.
    virtual const net_base::NetworkConfig& GetCurrentConfig() const = 0;
    // Called whenever a new network validation result or captive portal
    // detection result becomes available.
    virtual void OnNetworkMonitorResult(const Result& result) = 0;
    // Called when the validation trial triggered by NetworkMonitor::Start()
    // has been finished.
    virtual void OnValidationStarted(bool is_success) = 0;
  };

  NetworkMonitor(EventDispatcher* dispatcher,
                 Metrics* metrics,
                 ClientNetwork* client,
                 Technology technology,
                 int interface_index,
                 std::string_view interface,
                 PortalDetector::ProbingConfiguration probing_configuration,
                 ValidationMode validation_mode,
                 std::unique_ptr<ValidationLog> network_validation_log,
                 std::string_view logging_tag = "",
                 std::unique_ptr<CapportProxyFactory> capport_proxy_factory =
                     std::make_unique<CapportProxyFactory>(),
                 std::unique_ptr<ConnectionDiagnosticsFactory>
                     connection_diagnostics_factory =
                         std::make_unique<ConnectionDiagnosticsFactory>());
  virtual ~NetworkMonitor();

  // It's neither copyable nor movable.
  NetworkMonitor(const NetworkMonitor&) = delete;
  NetworkMonitor& operator=(const NetworkMonitor&) = delete;

  // Starts or restarts network validation and reschedule a network validation
  // attempt if necessary. Depending on the current stage of network validation
  // (rows) and |reason| (columns), different effects are possible as summarized
  // in the table:
  //
  //             |  IP provisioning   |  schedule attempt  |      do not
  //             |       event        |    immediately     |     reschedule
  // ----------- +--------------------+--------------------+--------------------
  //  validation |                    |                    |
  //   stopped   |         a)         |         a)         |         a)
  // ------------+--------------------+--------------------+--------------------
  //   attempt   |                    |                    |
  //  scheduled  |         a)         |         b)         |         d)
  // ------------+--------------------+--------------------+--------------------
  //  currently  |                    |                    |
  //   running   |         a)         |         c)         |         d)
  // ------------+--------------------+--------------------+--------------------
  //   a) reinitialize |portal_detector_| & start a network validation attempt
  //      immediately.
  //   b) reschedule the next network validation attempt to run immediately.
  //   c) reschedule another network validation attempt immediately after the
  //      current one if the result is not conclusive (the result was not
  //      kInternetConnectivity or kPortalRedirect).
  //   d) do nothing, wait for the network validation attempt scheduled next to
  //      run.
  mockable void Start(ValidationReason reason);

  // Stops the current attempt. No-op and returns false if no attempt is
  // running.
  mockable bool Stop();

  // Returns true if network validation is currently running.
  mockable bool IsRunning() const;

  // Sets the CAPPORT API and records the source of the API.
  mockable void SetCapportAPI(const net_base::HttpUrl& capport_api,
                              CapportSource source);

  // Sets and gets the current network validation mode.
  // TODO(b/314693271): update the state of |portal_detector_| appropriately
  // when the validation mode changes.
  mockable void SetValidationMode(ValidationMode mode);
  mockable ValidationMode GetValidationMode() { return validation_mode_; }

  // Injects the PortalDetector for testing.
  void set_portal_detector_for_testing(
      std::unique_ptr<PortalDetector> portal_detector);

  // Injects the CapportProxy for testing.
  void set_capport_proxy_for_testing(
      std::unique_ptr<CapportProxy> capport_proxy);

  // Exposes the callbacks to public for testing.
  void OnPortalDetectorResultForTesting(const PortalDetector::Result& result);
  void OnCapportStatusReceivedForTesting(
      const std::optional<CapportStatus>& status);

 private:
  // Starts the validation. Returns true is the validation has been successfully
  // started.
  bool StartValidationTask(ValidationReason reason);

  // Callback when |portal_detector_| returns the result.
  void OnPortalDetectorResult(const PortalDetector::Result& result);

  // Callback when |capport_proxy_| returns the result.
  void OnCapportStatusReceived(const std::optional<CapportStatus>& status);

  // Returns true if we need to send the |new_result| back to client.
  // The two parameters are either |result_from_portal_detector_| or
  // |result_from_capport_proxy_|.
  bool ShouldSendNewResult(const std::optional<Result>& new_result,
                           const std::optional<Result>& other_result) const;

  // Stops the |validation_log_| and records metrics.
  void StopNetworkValidationLog();

  // Initiates connection diagnostics on this Network.
  void StartConnectionDiagnostics();

  // These instances outlive this NetworkMonitor instance.
  EventDispatcher* dispatcher_;
  Metrics* metrics_;
  ClientNetwork* client_;

  // These instances are not changed during the whole lifetime.
  Technology technology_;
  int interface_index_;
  std::string interface_;
  std::string logging_tag_;
  PortalDetector::ProbingConfiguration probing_configuration_;
  ValidationMode validation_mode_;

  // The lifetime of these instances are the same as the NetworkMonitor.
  TrialScheduler trial_scheduler_;
  std::unique_ptr<PortalDetector> portal_detector_;

  std::unique_ptr<CapportProxyFactory> capport_proxy_factory_;
  // The CAPPORT proxy, only valid when the CAPPORT API was set.
  std::unique_ptr<CapportProxy> capport_proxy_;

  // The results converted from |portal_detector_| and |capport_proxy_|. The
  // value is reset when |portal_detector_| and |capport_proxy_| are triggered,
  // and is set when they return the result.
  std::optional<Result> result_from_portal_detector_;
  std::optional<Result> result_from_capport_proxy_;

  std::unique_ptr<ValidationLog> validation_log_;

  std::unique_ptr<ConnectionDiagnosticsFactory> connection_diagnostics_factory_;
  std::unique_ptr<ConnectionDiagnostics> connection_diagnostics_;
};

class NetworkMonitorFactory {
 public:
  NetworkMonitorFactory() = default;
  virtual ~NetworkMonitorFactory() = default;

  mockable std::unique_ptr<NetworkMonitor> Create(
      EventDispatcher* dispatcher,
      Metrics* metrics,
      NetworkMonitor::ClientNetwork* client,
      Technology technology,
      int interface_index,
      std::string_view interface,
      PortalDetector::ProbingConfiguration probing_configuration,
      NetworkMonitor::ValidationMode validation_mode,
      std::unique_ptr<ValidationLog> network_validation_log,
      std::string_view logging_tag = "");
};

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationMode mode);
std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationReason reason);

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ResultOrigin result_origin);

std::ostream& operator<<(std::ostream& stream,
                         const NetworkMonitor::Result& result);

}  // namespace shill
#endif  // SHILL_NETWORK_NETWORK_MONITOR_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_MONITOR_H_
#define SHILL_NETWORK_NETWORK_MONITOR_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <base/containers/span.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/network/capport_proxy.h"
#include "shill/network/connection_diagnostics.h"
#include "shill/network/portal_detector.h"
#include "shill/network/trial_scheduler.h"
#include "shill/technology.h"

namespace shill {

// Forward declaration to avoid circular inclusion.
class ValidationLog;

// The NetworkMonitor class monitors the general Internet connectivity and the
// existence of the captive portal by triggering the PortalDetector and
// CapportClient. Also, the class sends the network validation metrics.
class NetworkMonitor {
 public:
  // The extra delay that we wait for the CAPPORT becoming captive state again.
  static constexpr base::TimeDelta kCapportRemainingExtraDelay =
      base::Seconds(5);

  // Indicates the type of network validation to conduct on a connected Network.
  enum class ValidationMode {
    // Network validation with web probes is disabled. Captive portal detection
    // with CAPPORT or Passpoint R3 can still occur.
    kDisabled,
    // Network validation with web probes is enabled. Both HTTPS validation and
    // HTTP captive portal detection are performed.
    kFullValidation,
    // Only HTTP captive portal detection is performend. Network validation with
    // HTTPS probes is not performed. The result of network validation is never
    // kNoConnectivity.
    kHTTPOnly,
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
    // Retry the validation when the remaining time with external network access
    // from CAPPORT (is_captive==false) is over.
    kCapportTimeOver,
    // Retry the validation when the CAPPORT server is detected and the CAPPORT
    // functionality is turned on.
    kCapportEnabled,
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
                 patchpanel::Client* patchpanel_client,
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

  // Sets the CAPPORT server URL |capport_url| and records the source of the
  // URL. The URL should be resolved with |dns_list| specified from the same
  // source as the URL.
  mockable void SetCapportURL(const net_base::HttpUrl& capport_url,
                              base::span<const net_base::IPAddress> dns_list,
                              CapportSource source);

  // Sets and gets the current network validation mode.
  // TODO(b/314693271): update the state of |portal_detector_| appropriately
  // when the validation mode changes.
  mockable void SetValidationMode(ValidationMode mode);
  mockable ValidationMode GetValidationMode() { return validation_mode_; }

  // Setter/getter for enabling the CAPPORT functionality.
  mockable void SetCapportEnabled(bool enabled);
  bool GetCapportEnabled() const { return capport_enabled_; }

  // Sets the terms and conditions URL.
  mockable void SetTermsAndConditions(const net_base::HttpUrl& url);

  // Starts IPv4 and IPv6 connectivity diagnostics and IPv4 and IPv6 portal
  // detection if the IP family is configured and if there is no diagnostics
  // already running. Results are only logged.
  void StartConnectivityTest();

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

  // Initiates connection diagnostics on this Network for IPv4 or IPv6 if
  // |network_config| has a configuration for the corresponding IP family.
  void StartIPv4ConnectionDiagnostics(
      const net_base::NetworkConfig& network_config);
  void StartIPv6ConnectionDiagnostics(
      const net_base::NetworkConfig& network_config);
  // Initiates portal detection tests on this Network for IPv4 or IPv6 if
  // |network_config| has a configuration for the corresponding IP family.
  void StartIPv4PortalDetectorTest(
      const net_base::NetworkConfig& network_config);
  void IPv4PortalDetectorTestCallback(const PortalDetector::Result& result);
  void StartIPv6PortalDetectorTest(
      const net_base::NetworkConfig& network_config);
  void IPv6PortalDetectorTestCallback(const PortalDetector::Result& result);

  // These instances outlive this NetworkMonitor instance.
  EventDispatcher* dispatcher_;
  patchpanel::Client* patchpanel_client_;
  Metrics* metrics_;
  ClientNetwork* client_;

  // These instances are not changed during the whole lifetime.
  Technology technology_;
  int interface_index_;
  std::string interface_;
  std::string logging_tag_;
  PortalDetector::ProbingConfiguration probing_configuration_;

  ValidationMode validation_mode_;
  bool capport_enabled_ = true;

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
  std::unique_ptr<ConnectionDiagnostics> ipv4_connection_diagnostics_;
  std::unique_ptr<ConnectionDiagnostics> ipv6_connection_diagnostics_;
  // Other instances of PortalDetector used to evaluate IPv4 and IPv6 Internet
  // connectivity when Manager.CreateConnectivityReport is called.
  std::unique_ptr<PortalDetector> ipv4_connectivity_test_portal_detector_;
  std::unique_ptr<PortalDetector> ipv6_connectivity_test_portal_detector_;

  base::WeakPtrFactory<NetworkMonitor> weak_ptr_factory_for_capport_{this};
};

class NetworkMonitorFactory {
 public:
  NetworkMonitorFactory() = default;
  virtual ~NetworkMonitorFactory() = default;

  mockable std::unique_ptr<NetworkMonitor> Create(
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

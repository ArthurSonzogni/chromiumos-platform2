// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_MONITOR_H_
#define SHILL_NETWORK_NETWORK_MONITOR_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <net-base/http_url.h>

#include "shill/metrics.h"
#include "shill/mockable.h"
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

  // TODO(b/305129516): Define a dedicated struct when supporting CAPPORT.
  using Result = PortalDetector::Result;
  using ResultCallback = base::RepeatingCallback<void(const Result&)>;

  NetworkMonitor(
      EventDispatcher* dispatcher,
      Metrics* metrics,
      Technology technology,
      std::string_view interface,
      PortalDetector::ProbingConfiguration probing_configuration,
      ResultCallback result_callback,
      std::unique_ptr<ValidationLog> network_validation_log,
      std::string_view logging_tag = "",
      std::unique_ptr<PortalDetectorFactory> portal_detector_factory =
          std::make_unique<PortalDetectorFactory>());
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
  //   e) do nothing, wait for the network validation attempt scheduled next to
  //      run.
  mockable bool Start(ValidationReason reason,
                      net_base::IPFamily ip_family,
                      const std::vector<net_base::IPAddress>& dns_list);

  // Stops the current attempt. No-op and returns false if no attempt is
  // running.
  mockable bool Stop();

  // Sets the CAPPORT API and records the source of the API.
  mockable void SetCapportAPI(const net_base::HttpUrl& capport_api,
                              CapportSource source);

  // Injects the PortalDetector for testing.
  void set_portal_detector_for_testing(
      std::unique_ptr<PortalDetector> portal_detector);

 private:
  // Callback when |portal_detector_| returns the result.
  void OnPortalDetectorResult(const PortalDetector::Result& result);

  // Stops the |validation_log_| and records metrics.
  void StopNetworkValidationLog();

  EventDispatcher* dispatcher_;
  Metrics* metrics_;

  Technology technology_;
  std::string interface_;
  std::string logging_tag_;
  PortalDetector::ProbingConfiguration probing_configuration_;
  ResultCallback result_callback_;

  std::unique_ptr<PortalDetectorFactory> portal_detector_factory_;
  std::unique_ptr<PortalDetector> portal_detector_;

  std::unique_ptr<ValidationLog> validation_log_;

  base::WeakPtrFactory<NetworkMonitor> weak_ptr_factory_{this};
};

class NetworkMonitorFactory {
 public:
  NetworkMonitorFactory() = default;
  virtual ~NetworkMonitorFactory() = default;

  mockable std::unique_ptr<NetworkMonitor> Create(
      EventDispatcher* dispatcher,
      Metrics* metrics,
      Technology technology,
      std::string_view interface,
      PortalDetector::ProbingConfiguration probing_configuration,
      NetworkMonitor::ResultCallback result_callback,
      std::unique_ptr<ValidationLog> network_validation_log,
      std::string_view logging_tag = "");
};

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationReason reason);

}  // namespace shill
#endif  // SHILL_NETWORK_NETWORK_MONITOR_H_

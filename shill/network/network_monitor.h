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

#include "shill/mockable.h"
#include "shill/portal_detector.h"

namespace shill {

// The NetworkMonitor class monitors the general Internet connectivity and the
// existence of the captive portal by triggering the PortalDetector and
// CapportClient.
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

  // TODO(b/305129516): Define a dedicated struct when supporting CAPPORT.
  using Result = PortalDetector::Result;
  using ResultCallback = base::RepeatingCallback<void(const Result&)>;

  NetworkMonitor(
      EventDispatcher* dispatcher,
      std::string_view interface,
      PortalDetector::ProbingConfiguration probing_configuration,
      ResultCallback result_callback,
      std::string_view logging_tag = "",
      std::unique_ptr<PortalDetectorFactory> portal_detector_factory =
          std::make_unique<PortalDetectorFactory>());
  virtual ~NetworkMonitor();

  // It's neither copyable nor movable.
  NetworkMonitor(const NetworkMonitor&) = delete;
  NetworkMonitor& operator=(const NetworkMonitor&) = delete;

  // Starts a new validation attempt.
  mockable bool Start(ValidationReason reason,
                      net_base::IPFamily ip_family,
                      const std::vector<net_base::IPAddress>& dns_list);

  // Stops the current attempt.
  mockable void Stop();

  // Injects the PortalDetector for testing.
  void set_portal_detector_for_testing(
      std::unique_ptr<PortalDetector> portal_detector);

 private:
  void OnPortalDetectorResult(const PortalDetector::Result& result);

  EventDispatcher* dispatcher_;
  std::string interface_;
  std::string logging_tag_;
  PortalDetector::ProbingConfiguration probing_configuration_;
  ResultCallback result_callback_;

  std::unique_ptr<PortalDetectorFactory> portal_detector_factory_;
  std::unique_ptr<PortalDetector> portal_detector_;

  base::WeakPtrFactory<NetworkMonitor> weak_ptr_factory_{this};
};

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationReason reason);

}  // namespace shill
#endif  // SHILL_NETWORK_NETWORK_MONITOR_H_

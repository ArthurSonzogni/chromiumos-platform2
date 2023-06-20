// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MULTICAST_METRICS_H_
#define PATCHPANEL_MULTICAST_METRICS_H_

#include <memory>
#include <string>

#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include "base/strings/string_piece.h"
#include <base/timer/timer.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/shill_client.h"

namespace patchpanel {

// Placeholder interface name to be used for metrics poller which does not need
// to track active interface.
constexpr base::StringPiece kPlaceholderIfname = "placeholder0";

// Class to fetch and report multicast packet counts to UMA.
// This class reports 3 types of multicast metrics:
// - Device's total packet count.
// - Per-network-technology packet count.
// - ARC packet count.
class MulticastMetrics {
 public:
  // Enum type to report different multicast metrics. This distinction is
  // necessary as the polling lifetime of each type is different:
  // - Total: started and stopped on device startup and shutdown.
  // - Ethernet / WiFi: started and stopped whenever the related is connected
  // and disconnected
  // - ARC: started and stopped whenever ARC is started and multicast forwarding
  // state is changed.
  enum class Type {
    kTotal = 0,
    kEthernet = 1,
    kWiFi = 2,
    kARC = 3,
  };

  MulticastMetrics();
  MulticastMetrics(const MulticastMetrics&) = delete;
  MulticastMetrics& operator=(const MulticastMetrics&) = delete;
  ~MulticastMetrics() = default;

  // Start or stop polling for multicast packet count. When it is used for
  // network technology metrics, the interface name |ifname| needs to be set.
  // These methods are idempotent, calling the methods multiple times is safe.
  void Start(Type type, base::StringPiece ifname = kPlaceholderIfname);
  void Stop(Type type, base::StringPiece ifname = kPlaceholderIfname);

  // Start or stop polling for multicast packet count on device events.
  void OnIPConfigsChanged(const ShillClient::Device& device);
  void OnPhysicalDeviceAdded(const ShillClient::Device& device);
  void OnPhysicalDeviceRemoved(const ShillClient::Device& device);

  // Track ARC state to emit ARC metrics.
  void OnARCStarted();
  void OnARCStopped();

  // Restart polling on ARC multicast forwarder state changed. This method is
  // expected to only be called for WiFi. When polling is not running, this
  // method does nothing.
  void OnARCWiFiForwarderStarted();
  void OnARCWiFiForwarderStopped();

 private:
  // Handles polling to fetch and report UMA metrics.
  class Poller {
   public:
    Poller(Type type, MulticastMetrics* metrics);
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    ~Poller() = default;

    // Start and stop polling for multicast packet count. When stopping, metrics
    // will not be emitted. This is done to:
    // - avoid inaccurate metrics of a bursty traffic in a short period of time.
    // - allow metrics to be reported correctly on suspend resume.
    // - allow metrics to be reported as a count as opposed to rate, meaning the
    // polling time must be constant.
    void Start(base::StringPiece ifname);
    void Stop(base::StringPiece ifname);

    // Multicast metrics are only emitted when ARC is running.
    void UpdateARCState(bool running);

    // When ARC multicast forwarding state changed, different metrics are
    // supposed to be emitted. Restart the poll with the new state. For ARC
    // multicast, "active" and "inactive" metrics are  expected to be emitted.
    void UpdateARCForwarderState(bool enabled);

    void Record();

    // Added for testing.
    base::flat_set<std::string> ifnames();
    bool IsTimerRunning();
    bool IsARCForwardingEnabled();

   private:
    MulticastMetrics::Type type_;

    // Indicates whether or not ARC is running. ARC metrics are only emitted
    // when ARC is running.
    bool arc_running_ = false;

    // Indicates whether or not multicast forwarder is running for ARC. When it
    // is not running, ARC is expected to not get multicast packets.
    bool arc_fwd_enabled_ = false;

    // Active interface names. Poll is started whenever this is not empty and
    // stopped whenever this is empty. For metrics that does not track interface
    // names, the entry would be a placeholder string.
    base::flat_set<std::string> ifnames_;

    // TODO(jasongustaman): Replace with std::map after multicast counters
    // landed.
    uint64_t mdns_packet_count_ = 0;
    uint64_t ssdp_packet_count_ = 0;

    MulticastMetrics* metrics_;

    // Timer to continuously calls fetch packet count and report to UMA. When
    // this is destroyed, the continuous call is stopped as well.
    base::RepeatingTimer timer_;
  };

  FRIEND_TEST(MulticastMetricsTest, BaseState);
  FRIEND_TEST(MulticastMetricsTest, Total_StartStop);
  FRIEND_TEST(MulticastMetricsTest, NetworkTechnology_StartStop);
  FRIEND_TEST(MulticastMetricsTest, IPConfigChanges_StartStop);
  FRIEND_TEST(MulticastMetricsTest, DeviceChanges_StartStop);
  FRIEND_TEST(MulticastMetricsTest, MultipleDeviceChanges_StartStop);
  FRIEND_TEST(MulticastMetricsTest, ARC_StartStop);
  FRIEND_TEST(MulticastMetricsTest, ARC_ForwardingStateChanges);
  FRIEND_TEST(MulticastMetricsTest, ARC_StartStopWithForwardingChanges);

  // Pollers to handle each metrics type and poll. This is instantiated at the
  // constructor of the class.
  base::flat_map<Type, std::unique_ptr<Poller>> pollers_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MULTICAST_METRICS_H_

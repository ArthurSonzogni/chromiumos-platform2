// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_metrics.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/containers/contains.h>
#include <base/containers/fixed_flat_map.h>
#include <base/containers/fixed_flat_set.h>
#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "patchpanel/metrics.h"
#include "patchpanel/multicast_counters_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

// Maximum recorded packet count for the multicast metrics, equivalent to 30
// packets per second.
constexpr int kPacketCountMax = 30 * kMulticastPollDelay.InSeconds();
constexpr int kPacketCountBuckets = 100;

std::optional<MulticastMetrics::Type> ShillDeviceTypeToMulticastMetricsType(
    ShillClient::Device::Type type) {
  switch (type) {
    case ShillClient::Device::Type::kEthernet:
      return MulticastMetrics::Type::kEthernet;
    case ShillClient::Device::Type::kWifi:
      return MulticastMetrics::Type::kWiFi;
    default:
      // Invalid multicast metrics type.
      return std::nullopt;
  }
}

std::string MulticastMetricsTypeToString(MulticastMetrics::Type type) {
  switch (type) {
    case MulticastMetrics::Type::kTotal:
      return "Total";
    case MulticastMetrics::Type::kEthernet:
      return "Ethernet";
    case MulticastMetrics::Type::kWiFi:
      return "WiFi";
    case MulticastMetrics::Type::kARC:
      return "ARC";
  }
}

// Map of multicast protocol to Ethernet metrics name.
static constexpr auto kEthernetMetricNames = base::MakeFixedFlatMap<
    std::optional<MulticastCountersService::MulticastProtocolType>,
    std::string_view>({
    {std::nullopt, kMulticastEthernetConnectedCountMetrics},
    {MulticastCountersService::MulticastProtocolType::kMdns,
     kMulticastEthernetMDNSConnectedCountMetrics},
    {MulticastCountersService::MulticastProtocolType::kSsdp,
     kMulticastEthernetSSDPConnectedCountMetrics},
});

// Map of multicast protocol to WiFi metrics name.
static constexpr auto kWiFiMetricNames = base::MakeFixedFlatMap<
    std::optional<MulticastCountersService::MulticastProtocolType>,
    std::string_view>({
    {std::nullopt, kMulticastWiFiConnectedCountMetrics},
    {MulticastCountersService::MulticastProtocolType::kMdns,
     kMulticastWiFiMDNSConnectedCountMetrics},
    {MulticastCountersService::MulticastProtocolType::kSsdp,
     kMulticastWiFiSSDPConnectedCountMetrics},
});

// Map of pair of ARC multicast forwarder status and multicast protocol to
// metrics name.
static constexpr auto kARCMetricNames = base::MakeFixedFlatMap<
    std::pair<bool, MulticastCountersService::MulticastProtocolType>,
    std::string_view>({
    {{/*arc_fwd_enabled=*/true,
      MulticastCountersService::MulticastProtocolType::kMdns},
     kMulticastARCWiFiMDNSActiveCountMetrics},
    {{/*arc_fwd_enabled=*/true,
      MulticastCountersService::MulticastProtocolType::kSsdp},
     kMulticastARCWiFiSSDPActiveCountMetrics},
    {{/*arc_fwd_enabled=*/false,
      MulticastCountersService::MulticastProtocolType::kMdns},
     kMulticastARCWiFiMDNSInactiveCountMetrics},
    {{/*arc_fwd_enabled=*/false,
      MulticastCountersService::MulticastProtocolType::kSsdp},
     kMulticastARCWiFiSSDPInactiveCountMetrics},
});

// Get metrics name for UMA.
std::optional<std::string_view> GetMetricsName(
    MulticastMetrics::Type type,
    std::optional<MulticastCountersService::MulticastProtocolType> protocol,
    std::optional<bool> arc_fwd_enabled) {
  switch (type) {
    case MulticastMetrics::Type::kTotal:
      if (protocol) {
        // No need to report specific multicast protocol metrics for total.
        return std::nullopt;
      }
      return kMulticastTotalCountMetrics;
    case MulticastMetrics::Type::kEthernet:
      if (!kEthernetMetricNames.contains(protocol)) {
        return std::nullopt;
      }
      return kEthernetMetricNames.at(protocol);
    case MulticastMetrics::Type::kWiFi:
      if (!kWiFiMetricNames.contains(protocol)) {
        return std::nullopt;
      }
      return kWiFiMetricNames.at(protocol);
    case MulticastMetrics::Type::kARC:
      if (!arc_fwd_enabled || !protocol) {
        // Only report specific multicast protocol metrics for ARC.
        return std::nullopt;
      }
      std::pair<bool, MulticastCountersService::MulticastProtocolType> key = {
          *arc_fwd_enabled, *protocol};
      if (!kARCMetricNames.contains(key)) {
        return std::nullopt;
      }
      return kARCMetricNames.at(key);
  }
}

// Contains accepted multicast metrics type for each multicast counters
// technology.
static constexpr auto kAccepted = base::MakeFixedFlatSet<
    std::pair<MulticastCountersService::MulticastTechnologyType,
              MulticastMetrics::Type>>({
    {MulticastCountersService::MulticastTechnologyType::kEthernet,
     MulticastMetrics::Type::kTotal},
    {MulticastCountersService::MulticastTechnologyType::kEthernet,
     MulticastMetrics::Type::kEthernet},
    {MulticastCountersService::MulticastTechnologyType::kWifi,
     MulticastMetrics::Type::kTotal},
    {MulticastCountersService::MulticastTechnologyType::kWifi,
     MulticastMetrics::Type::kWiFi},
    {MulticastCountersService::MulticastTechnologyType::kWifi,
     MulticastMetrics::Type::kARC},
});

}  // namespace

MulticastMetrics::MulticastMetrics(MulticastCountersService* counters_service,
                                   MetricsLibraryInterface* metrics)
    : counters_service_(counters_service), metrics_lib_(metrics) {
  pollers_.emplace(Type::kTotal, std::make_unique<Poller>(Type::kTotal, this));
  pollers_.emplace(Type::kEthernet,
                   std::make_unique<Poller>(Type::kEthernet, this));
  pollers_.emplace(Type::kWiFi, std::make_unique<Poller>(Type::kWiFi, this));
  pollers_.emplace(Type::kARC, std::make_unique<Poller>(Type::kARC, this));
}

void MulticastMetrics::Start(MulticastMetrics::Type type,
                             std::string_view ifname) {
  pollers_[type]->Start(ifname);
}

void MulticastMetrics::Stop(MulticastMetrics::Type type,
                            std::string_view ifname) {
  pollers_[type]->Stop(ifname);
}

void MulticastMetrics::OnIPConfigsChanged(const ShillClient::Device& device) {
  auto type = ShillDeviceTypeToMulticastMetricsType(device.type);
  if (!type) {
    return;
  }

  // Handle network technology specific pollers.
  if (device.IsConnected()) {
    Start(*type, device.ifname);
  } else {
    Stop(*type, device.ifname);
  }

  // Handle ARC pollers.
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (device.IsConnected()) {
    Start(Type::kARC, device.ifname);
  } else {
    Stop(Type::kARC, device.ifname);
  }
}

void MulticastMetrics::OnPhysicalDeviceAdded(
    const ShillClient::Device& device) {
  auto type = ShillDeviceTypeToMulticastMetricsType(device.type);
  if (!type) {
    return;
  }

  // Handle network technology specific pollers.
  if (device.IsConnected()) {
    Start(*type, device.ifname);
  }

  // Handle ARC pollers.
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (device.IsConnected()) {
    Start(Type::kARC, device.ifname);
  }
}

void MulticastMetrics::OnPhysicalDeviceRemoved(
    const ShillClient::Device& device) {
  auto type = ShillDeviceTypeToMulticastMetricsType(device.type);
  if (!type) {
    return;
  }

  // Handle network technology specific pollers.
  Stop(*type, device.ifname);

  // Handle ARC pollers.
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  Stop(Type::kARC, device.ifname);
}

void MulticastMetrics::OnARCStarted() {
  pollers_[Type::kARC]->UpdateARCState(/*running=*/true);
}

void MulticastMetrics::OnARCStopped() {
  pollers_[Type::kARC]->UpdateARCState(/*running=*/false);
}

void MulticastMetrics::OnARCWiFiForwarderStarted() {
  pollers_[Type::kARC]->UpdateARCForwarderState(/*enabled=*/true);
}

void MulticastMetrics::OnARCWiFiForwarderStopped() {
  pollers_[Type::kARC]->UpdateARCForwarderState(/*enabled=*/false);
}

std::optional<
    std::map<MulticastCountersService::MulticastProtocolType, uint64_t>>
MulticastMetrics::GetCounters(Type type) {
  if (!counters_service_) {
    LOG(ERROR) << "Empty multicast counters service";
    return std::nullopt;
  }
  auto counters = counters_service_->GetCounters();
  if (!counters) {
    return std::nullopt;
  }

  std::map<MulticastCountersService::MulticastProtocolType, uint64_t> ret = {
      {MulticastCountersService::MulticastProtocolType::kMdns, 0},
      {MulticastCountersService::MulticastProtocolType::kSsdp, 0}};
  for (const auto& counter : *counters) {
    const MulticastCountersService::CounterKey& key = counter.first;
    if (kAccepted.contains({key.second, type})) {
      ret[key.first] += counter.second;
    }
  }

  return ret;
}

void MulticastMetrics::SendPacketCountMetrics(
    Type type,
    uint64_t packet_count,
    std::optional<MulticastCountersService::MulticastProtocolType> protocol,
    std::optional<bool> arc_fwd_enabled) {
  if (!metrics_lib_) {
    LOG(ERROR) << "Metrics client is not valid";
    return;
  }

  auto metrics_name = GetMetricsName(type, protocol, arc_fwd_enabled);
  if (!metrics_name) {
    LOG(ERROR) << "Trying to send invalid metrics";
    return;
  }

  if (packet_count > kPacketCountMax) {
    packet_count = kPacketCountMax;
  }
  metrics_lib_->SendToUMA(std::string(*metrics_name),
                          static_cast<int>(packet_count),
                          /*min=*/0, kPacketCountMax, kPacketCountBuckets);
}

void MulticastMetrics::SendARCActiveTimeMetrics(
    base::TimeDelta multicast_enabled_duration,
    base::TimeDelta wifi_enabled_duration) {
  if (!metrics_lib_) {
    LOG(ERROR) << "Metrics client is not valid";
    return;
  }
  if (wifi_enabled_duration.InSeconds() == 0) {
    return;
  }
  metrics_lib_->SendPercentageToUMA(
      kMulticastActiveTimeMetrics,
      static_cast<int>((100 * multicast_enabled_duration.InSeconds() /
                        wifi_enabled_duration.InSeconds())));
}

MulticastMetrics::Poller::Poller(MulticastMetrics::Type type,
                                 MulticastMetrics* metrics)
    : type_(type), metrics_(metrics) {}

void MulticastMetrics::Poller::Start(std::string_view ifname) {
  // Do nothing if poll is already started.
  if (base::Contains(ifnames_, ifname)) {
    return;
  }
  ifnames_.insert(std::string(ifname));
  if (ifnames_.size() > 1) {
    return;
  }
  // For ARC, poll is only started whenever there is at least one WiFi interface
  // connected and ARC is running. Keep track of the states.
  if (type_ == MulticastMetrics::Type::kARC && !arc_running_) {
    return;
  }

  StartTimer();
  total_arc_multicast_enabled_duration_ = base::Seconds(0);
  total_arc_wifi_connection_duration_ = base::Seconds(0);
}

void MulticastMetrics::Poller::Stop(std::string_view ifname) {
  // Do nothing if poll is already stopped.
  auto num_removed = ifnames_.erase(std::string(ifname));
  if (num_removed == 0 || !ifnames_.empty()) {
    return;
  }
  if (type_ == MulticastMetrics::Type::kARC && !arc_running_) {
    return;
  }
  StopTimer();

  UpdateARCActiveTimeDuration(IsARCForwardingEnabled());
  if (type_ == MulticastMetrics::Type::kARC) {
    metrics_->SendARCActiveTimeMetrics(total_arc_multicast_enabled_duration_,
                                       total_arc_wifi_connection_duration_);
  }
}

void MulticastMetrics::Poller::UpdateARCState(bool running) {
  if (arc_running_ == running) {
    return;
  }
  arc_running_ = running;

  // Do nothing if there is no active WiFi device.
  if (ifnames_.empty()) {
    return;
  }

  if (arc_running_) {
    StartTimer();
  } else {
    StopTimer();
  }
}

void MulticastMetrics::Poller::UpdateARCForwarderState(bool enabled) {
  if (arc_fwd_enabled_ == enabled) {
    return;
  }
  arc_fwd_enabled_ = enabled;

  if (!arc_running_) {
    return;
  }

  // We add all time intervals between ARC multicast forwarder state update
  // to wifi connection duration, and only add those between enable and disable
  // of ARC multicast forwarder to multicast enabled duration.
  // Since update active time duration is based on previous ARC forwarder
  // state, negate enable state here.
  UpdateARCActiveTimeDuration(!enabled);

  // Restart polling to emit different metrics.
  StopTimer();
  StartTimer();
}

void MulticastMetrics::Poller::StartTimer() {
  if (!metrics_) {
    return;
  }

  auto packet_counts = metrics_->GetCounters(type_);
  if (!packet_counts) {
    LOG(ERROR) << "Failed to fetch multicast packet counts";
    return;
  }
  packet_counts_ = *packet_counts;
  last_record_timepoint_ = base::TimeTicks::Now();
  timer_.Start(FROM_HERE, kMulticastPollDelay, this,
               &MulticastMetrics::Poller::Record);
}

void MulticastMetrics::Poller::StopTimer() {
  timer_.Stop();
  packet_counts_.clear();
}

void MulticastMetrics::Poller::Record() {
  if (!metrics_) {
    return;
  }
  auto packet_counts = metrics_->GetCounters(type_);
  if (!packet_counts) {
    LOG(ERROR) << "Failed to get multicast packet counts for "
               << MulticastMetricsTypeToString(type_);
    return;
  }

  // Send specific multicast protocol packet count metrics.
  uint64_t total_packet_count = 0;
  for (const auto& packet_count : *packet_counts) {
    uint64_t count = packet_count.second - packet_counts_[packet_count.first];
    total_packet_count += count;

    if (type_ == Type::kTotal) {
      // No need to report specific multicast protocol metrics for total.
      continue;
    }
    metrics_->SendPacketCountMetrics(type_, count, packet_count.first,
                                     arc_fwd_enabled_);
  }
  packet_counts_ = *packet_counts;

  if (type_ == Type::kARC) {
    // Update active time duration based on ARC forwarder state.
    UpdateARCActiveTimeDuration(IsARCForwardingEnabled());
    return;
  }

  // Send total packet count metrics. This is not sent for |type_| == kARC.
  metrics_->SendPacketCountMetrics(type_, total_packet_count);
}

base::flat_set<std::string> MulticastMetrics::Poller::ifnames() {
  return ifnames_;
}

bool MulticastMetrics::Poller::IsTimerRunning() {
  return timer_.IsRunning();
}

bool MulticastMetrics::Poller::IsARCForwardingEnabled() {
  return arc_fwd_enabled_;
}

void MulticastMetrics::Poller::UpdateARCActiveTimeDuration(
    bool prev_arc_multicast_fwd_running) {
  auto duration = base::TimeTicks::Now() - last_record_timepoint_;
  last_record_timepoint_ = base::TimeTicks::Now();

  total_arc_wifi_connection_duration_ += duration;
  if (prev_arc_multicast_fwd_running) {
    total_arc_multicast_enabled_duration_ += duration;
  }
}

}  // namespace patchpanel

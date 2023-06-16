// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_metrics.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/containers/contains.h>
#include <base/containers/fixed_flat_set.h>
#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include <base/logging.h>
#include "base/strings/string_piece.h"
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "patchpanel/metrics.h"
#include "patchpanel/multicast_counters_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

// Poll delay to fetch multicast packet count and report to UMA.
constexpr base::TimeDelta kPollDelay = base::Minutes(2);

// If interval between two records spend more than kPollDelay +
// kPollDelayJitter, it means there is a suspend and we should discard the
// data.
constexpr base::TimeDelta kPollDelayJitter = base::Seconds(10);

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
                             base::StringPiece ifname) {
  pollers_[type]->Start(ifname);
}

void MulticastMetrics::Stop(MulticastMetrics::Type type,
                            base::StringPiece ifname) {
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

void MulticastMetrics::Poller::Start(base::StringPiece ifname) {
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

void MulticastMetrics::Poller::Stop(base::StringPiece ifname) {
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
  last_record_timepoint_ = base::Time::Now();
  timer_.Start(FROM_HERE, kPollDelay, this, &MulticastMetrics::Poller::Record);
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

  // Get the multicast packet count between the polls.
  std::map<MulticastCountersService::MulticastProtocolType, uint64_t> diff;
  for (const auto& packet_count : *packet_counts) {
    diff.emplace(packet_count.first,
                 packet_count.second - packet_counts_[packet_count.first]);
  }
  packet_counts_ = *packet_counts;

  // TODO(jasongustaman): Record packet count diff to UMA.

  // Update active time duration based on ARC forwarder state.
  UpdateARCActiveTimeDuration(IsARCForwardingEnabled());
  switch (type_) {
    case MulticastMetrics::Type::kTotal:
    case MulticastMetrics::Type::kEthernet:
    case MulticastMetrics::Type::kWiFi:
    case MulticastMetrics::Type::kARC:
      return;
    default:
      LOG(ERROR) << "Unexpected MulticastMetrics::Type: "
                 << static_cast<int>(type_);
      return;
  }
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
  auto duration = base::Time::Now() - last_record_timepoint_;
  last_record_timepoint_ = base::Time::Now();

  // When system is suspended the time elapsed from last checkpoint
  // will be longer than usual and we should discard the data.
  if (duration > (kPollDelay + kPollDelayJitter)) {
    return;
  }
  total_arc_wifi_connection_duration_ += duration;
  if (prev_arc_multicast_fwd_running) {
    total_arc_multicast_enabled_duration_ += duration;
  }
}

}  // namespace patchpanel

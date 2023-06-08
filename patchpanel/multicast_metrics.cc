// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_metrics.h"

#include <memory>
#include <optional>
#include <string>

#include <base/containers/contains.h>
#include <base/containers/flat_map.h>
#include <base/logging.h>
#include "base/strings/string_piece.h"
#include <base/time/time.h>

#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

// Poll delay to fetch multicast packet count and report to UMA.
constexpr base::TimeDelta kPollDelay = base::Minutes(2);

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

}  // namespace

MulticastMetrics::MulticastMetrics() {
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
  if (device.IsConnected()) {
    Start(*type, device.ifname);
  } else {
    Stop(*type, device.ifname);
  }
}

void MulticastMetrics::OnPhysicalDeviceAdded(
    const ShillClient::Device& device) {
  auto type = ShillDeviceTypeToMulticastMetricsType(device.type);
  if (!type) {
    return;
  }
  if (device.IsConnected()) {
    Start(*type, device.ifname);
  }
}

void MulticastMetrics::OnPhysicalDeviceRemoved(
    const ShillClient::Device& device) {
  auto type = ShillDeviceTypeToMulticastMetricsType(device.type);
  if (!type) {
    return;
  }
  Stop(*type, device.ifname);
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

  // TODO(jasongustaman): Set current packet count by from counters service.
  mdns_packet_count_ = 0;
  ssdp_packet_count_ = 0;
  timer_.Start(FROM_HERE, kPollDelay, this, &MulticastMetrics::Poller::Record);
}

void MulticastMetrics::Poller::Stop(base::StringPiece ifname) {
  // Do nothing if poll is already stopped.
  auto num_removed = ifnames_.erase(std::string(ifname));
  if (num_removed == 0 || !ifnames_.empty()) {
    return;
  }

  timer_.Stop();
  mdns_packet_count_ = 0;
  ssdp_packet_count_ = 0;
}

void MulticastMetrics::Poller::Record() {
  if (!metrics_) {
    return;
  }

  // TODO(jasongustaman): Fetch packet count from counters service and record
  // the diff to UMA.
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

}  // namespace patchpanel

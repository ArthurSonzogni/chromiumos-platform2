// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network_monitor_service.h"

#include <memory>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <utility>

#include <base/bind.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/net/rtnl_handler.h"
#include "shill/net/rtnl_listener.h"

namespace patchpanel {

namespace {
// The set of states which indicate the neighbor is valid. Copied from
// /include/net/neighbour.h in linux kernel.
constexpr uint16_t kNUDStateValid = NUD_PERMANENT | NUD_NOARP | NUD_REACHABLE |
                                    NUD_PROBE | NUD_STALE | NUD_DELAY;

std::string NUDStateToString(uint16_t state) {
  switch (state) {
    case NUD_INCOMPLETE:
      return "NUD_INCOMPLETE";
    case NUD_REACHABLE:
      return "NUD_REACHABLE";
    case NUD_STALE:
      return "NUD_STALE";
    case NUD_DELAY:
      return "NUD_DELAY";
    case NUD_PROBE:
      return "NUD_PROBE";
    case NUD_FAILED:
      return "NUD_FAILED";
    case NUD_NOARP:
      return "NUD_NOARP";
    case NUD_PERMANENT:
      return "NUD_PERMANENT";
    case NUD_NONE:
      return "NUD_NONE";
    default:
      return "Unknown NUD state " + std::to_string(state);
  }
}

bool IsIPv6LinkLocalAddress(const shill::IPAddress& addr) {
  if (addr.family() != shill::IPAddress::kFamilyIPv6)
    return false;
  return shill::IPAddress("fe80::", 64).CanReachAddress(addr);
}

// We cannot set the state of an address to NUD_PROBE when the kernel doesn't
// know its MAC address, and thus the state should be in NUD_VALID. We don't
// probe for the other states in NUD_VALID because:
// - NUD_DELAY will soon become NUD_PROBE or NUD_REACHABLE;
// - NUD_PROBE means the kernel is already probing;
// - NUD_PERMANENT and NUD_NOARP are special states and it will not be
// changed.
bool NeedProbeForState(uint16_t current_state) {
  return current_state & (NUD_STALE | NUD_REACHABLE);
}

}  // namespace

NeighborLinkMonitor::NeighborLinkMonitor(int ifindex,
                                         const std::string& ifname,
                                         shill::RTNLHandler* rtnl_handler)
    : ifindex_(ifindex), ifname_(ifname), rtnl_handler_(rtnl_handler) {}

NeighborLinkMonitor::WatchingEntry::WatchingEntry(shill::IPAddress addr,
                                                  Role role)
    : addr(std::move(addr)), role_flags(static_cast<uint8_t>(role)) {}

std::string NeighborLinkMonitor::WatchingEntry::ToString() const {
  return "(addr=" + addr.ToString() + ", role=" +
         (role_flags & static_cast<uint8_t>(Role::kGateway) ? "gateway" : "") +
         (role_flags & static_cast<uint8_t>(Role::kDNSServer) ? "dns server"
                                                              : "") +
         ", state=" + NUDStateToString(nud_state) + ")";
}

void NeighborLinkMonitor::AddWatchingEntries(
    int prefix_length,
    const std::string& addr,
    const std::string& gateway,
    const std::vector<std::string>& dns_addrs) {
  shill::IPAddress gateway_addr(gateway);
  if (!gateway_addr.IsValid()) {
    LOG(ERROR) << "Gateway address " << gateway << " is not valid";
    return;
  }
  UpdateWatchingEntry(gateway_addr, WatchingEntry::Role::kGateway);

  shill::IPAddress local_addr(addr, prefix_length);
  if (!local_addr.IsValid()) {
    LOG(ERROR) << "Local address " << local_addr << " is not valid";
    return;
  }

  int watching_dns_num = 0;
  int skipped_dns_num = 0;
  for (const auto& dns : dns_addrs) {
    shill::IPAddress dns_addr(dns);
    if (!dns_addr.IsValid()) {
      LOG(ERROR) << "DNS server address is not valid";
      return;
    }
    if (!local_addr.CanReachAddress(dns_addr) &&
        !IsIPv6LinkLocalAddress(dns_addr)) {
      skipped_dns_num++;
      continue;
    }
    watching_dns_num++;
    UpdateWatchingEntry(dns_addr, WatchingEntry::Role::kDNSServer);
  }
  LOG(INFO) << shill::IPAddress::GetAddressFamilyName(local_addr.family())
            << " watching entries added on " << ifname_
            << ": skipped_dns_num=" << skipped_dns_num
            << " ,watching_dns_num=" << watching_dns_num;
}

void NeighborLinkMonitor::UpdateWatchingEntry(const shill::IPAddress& addr,
                                              WatchingEntry::Role role) {
  const auto it = watching_entries_.find(addr);
  if (it == watching_entries_.end())
    watching_entries_.insert(std::make_pair(addr, WatchingEntry(addr, role)));
  else
    it->second.role_flags |= static_cast<uint8_t>(role);
}

void NeighborLinkMonitor::OnIPConfigChanged(
    const ShillClient::IPConfig& ipconfig) {
  LOG(INFO) << "ipconfigs changed on " << ifname_ << ", reset watching entries";
  watching_entries_.clear();

  if (!ipconfig.ipv4_address.empty())
    AddWatchingEntries(ipconfig.ipv4_prefix_length, ipconfig.ipv4_address,
                       ipconfig.ipv4_gateway, ipconfig.ipv4_dns_addresses);
  if (!ipconfig.ipv6_address.empty())
    AddWatchingEntries(ipconfig.ipv6_prefix_length, ipconfig.ipv6_address,
                       ipconfig.ipv6_gateway, ipconfig.ipv6_dns_addresses);

  if (watching_entries_.empty()) {
    LOG(INFO) << "Stop due to empty watching list on " << ifname_;
    Stop();
    return;
  }

  Start();
}

void NeighborLinkMonitor::Start() {
  if (!listener_)
    listener_ = std::make_unique<shill::RTNLListener>(
        shill::RTNLHandler::kRequestNeighbor,
        base::BindRepeating(&NeighborLinkMonitor::OnNeighborMessage,
                            base::Unretained(this)),
        rtnl_handler_);

  probe_timer_.Stop();
  probe_timer_.Start(FROM_HERE, kActiveProbeInterval, this,
                     &NeighborLinkMonitor::ProbeAll);
  ProbeAll();
}

void NeighborLinkMonitor::Stop() {
  listener_ = nullptr;
  probe_timer_.Stop();
}

void NeighborLinkMonitor::ProbeAll() {
  for (const auto& addr_entry : watching_entries_) {
    const auto& entry = addr_entry.second;
    // If we know nothing about this address from the kernel, send a get request
    // first. Probe will be done on getting the response in OnNeighborMessage().
    if (entry.nud_state == NUD_NONE) {
      SendNeighborGetRTNLMessage(entry);
      continue;
    }

    if (!NeedProbeForState(entry.nud_state))
      continue;

    SendNeighborProbeRTNLMessage(entry);
  }
}

void NeighborLinkMonitor::SendNeighborGetRTNLMessage(
    const WatchingEntry& entry) {
  // |seq| will be set by RTNLHandler.
  auto msg = std::make_unique<shill::RTNLMessage>(
      shill::RTNLMessage::kTypeNeighbor, shill::RTNLMessage::kModeGet,
      NLM_F_REQUEST, 0 /* seq */, 0 /* pid */, ifindex_, entry.addr.family());
  msg->SetAttribute(NDA_DST, entry.addr.address());

  // TODO(jiejiang): We may get an error of errno=16 (Device or resource busy)
  // from kernel here. We may need to serialize the GET requests.
  if (!rtnl_handler_->SendMessage(std::move(msg), nullptr /* msg_seq */))
    LOG(WARNING) << "Failed to send neighbor get message for "
                 << entry.ToString() << " on " << ifname_;
}

void NeighborLinkMonitor::SendNeighborProbeRTNLMessage(
    const WatchingEntry& entry) {
  // |seq| will be set by RTNLHandler.
  auto msg = std::make_unique<shill::RTNLMessage>(
      shill::RTNLMessage::kTypeNeighbor, shill::RTNLMessage::kModeAdd,
      NLM_F_REQUEST | NLM_F_REPLACE, 0 /* seq */, 0 /* pid */, ifindex_,
      entry.addr.family());

  // We don't need to set |ndm_flags| and |ndm_type| for this message.
  msg->set_neighbor_status(shill::RTNLMessage::NeighborStatus(
      NUD_PROBE, 0 /* ndm_flags */, 0 /* ndm_type */));
  msg->SetAttribute(NDA_DST, entry.addr.address());

  if (!rtnl_handler_->SendMessage(std::move(msg), nullptr /* msg_seq */))
    LOG(WARNING) << "Failed to send neighbor probe message for "
                 << entry.ToString() << " on " << ifname_;
}

void NeighborLinkMonitor::OnNeighborMessage(const shill::RTNLMessage& msg) {
  if (msg.interface_index() != ifindex_)
    return;

  auto family = msg.family();
  shill::ByteString dst = msg.GetAttribute(NDA_DST);
  shill::IPAddress addr(family, dst);
  if (!addr.IsValid()) {
    LOG(WARNING) << "Got neighbor message with invalid addr " << addr;
    return;
  }

  const auto it = watching_entries_.find(addr);
  if (it == watching_entries_.end())
    return;

  uint16_t old_nud_state = it->second.nud_state;
  uint16_t new_nud_state;
  if (msg.mode() == shill::RTNLMessage::kModeDelete)
    new_nud_state = NUD_NONE;
  else
    new_nud_state = msg.neighbor_status().state;

  it->second.nud_state = new_nud_state;

  // Leaves a log when the neighbor becomes valid from invalid or vice versa.
  bool old_state_is_valid = old_nud_state & kNUDStateValid;
  bool new_state_is_valid = new_nud_state & kNUDStateValid;
  if (old_state_is_valid != new_state_is_valid) {
    LOG(INFO) << "NUD state changed on " << ifname_ << " for "
              << it->second.ToString()
              << ", old_state=" << NUDStateToString(old_nud_state);
    if (!new_state_is_valid)
      LOG(WARNING) << "A neighbor becomes invalid on " << ifname_ << " "
                   << it->second.ToString();
  }

  // Probes this entry if we know it for the first time (state changed
  // from NUD_NONE, e.g., the monitor just started, or this entry has been
  // removed once).
  if (old_nud_state == NUD_NONE && NeedProbeForState(new_nud_state))
    SendNeighborProbeRTNLMessage(it->second);
}

NetworkMonitorService::NetworkMonitorService(ShillClient* shill_client)
    : shill_client_(shill_client),
      rtnl_handler_(shill::RTNLHandler::GetInstance()) {}

void NetworkMonitorService::Start() {
  // Setups the RTNL socket and listens to neighbor events. This should be
  // called before creating NeighborLinkMonitors.
  rtnl_handler_->Start(RTMGRP_NEIGH);

  // Calls ScanDevices() first to make sure ShillClient knows all existing
  // devices in shill, and then triggers OnDevicesChanged() manually before
  // registering DevicesChangedHandler to make sure we see each device exactly
  // once.
  shill_client_->ScanDevices();
  OnDevicesChanged(shill_client_->get_devices(), {} /* removed */);
  shill_client_->RegisterDevicesChangedHandler(base::BindRepeating(
      &NetworkMonitorService::OnDevicesChanged, weak_factory_.GetWeakPtr()));

  shill_client_->RegisterIPConfigsChangedHandler(base::BindRepeating(
      &NetworkMonitorService::OnIPConfigsChanged, weak_factory_.GetWeakPtr()));
}

void NetworkMonitorService::OnDevicesChanged(
    const std::set<std::string>& added, const std::set<std::string>& removed) {
  for (const auto& device : added) {
    ShillClient::Device device_props;
    if (!shill_client_->GetDeviceProperties(device, &device_props)) {
      LOG(ERROR)
          << "Get device props failed. Skipped creating neighbor monitor on "
          << device;
      continue;
    }

    if (device_props.type != shill::kTypeWifi) {
      LOG(INFO) << "Skipped creating neighbor monitor for device with type="
                << device_props.type << " on " << device;
      continue;
    }

    int ifindex = if_nametoindex(device_props.ifname.c_str());
    if (ifindex == 0) {
      PLOG(ERROR) << "Could not obtain interface index for "
                  << device_props.ifname;
      continue;
    }

    auto link_monitor = std::make_unique<NeighborLinkMonitor>(
        ifindex, device_props.ifname, rtnl_handler_);
    link_monitor->OnIPConfigChanged(device_props.ipconfig);
    neighbor_link_monitors_[device] = std::move(link_monitor);
  }

  for (const auto& device : removed)
    neighbor_link_monitors_.erase(device);
}

void NetworkMonitorService::OnIPConfigsChanged(
    const std::string& device, const ShillClient::IPConfig& ipconfig) {
  const auto it = neighbor_link_monitors_.find(device);
  if (it == neighbor_link_monitors_.end())
    return;

  it->second->OnIPConfigChanged(ipconfig);
}

}  // namespace patchpanel

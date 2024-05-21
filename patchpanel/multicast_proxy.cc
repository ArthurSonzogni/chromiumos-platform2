// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_proxy.h"

#include <sysexits.h>

#include <utility>

#include <base/logging.h>

#include "patchpanel/ipc.h"
#include "patchpanel/minijailed_process_runner.h"

namespace patchpanel {

MulticastProxy::MulticastProxy(base::ScopedFD control_fd)
    : msg_dispatcher_(std::move(control_fd)) {
  msg_dispatcher_.RegisterFailureHandler(base::BindRepeating(
      &MulticastProxy::OnParentProcessExit, weak_factory_.GetWeakPtr()));

  msg_dispatcher_.RegisterMessageHandler(base::BindRepeating(
      &MulticastProxy::OnControlMessage, weak_factory_.GetWeakPtr()));
}

int MulticastProxy::OnInit() {
  // Prevent the main process from sending us any signals.
  if (setsid() < 0) {
    PLOG(ERROR) << "Failed to created a new session with setsid; exiting";
    return EX_OSERR;
  }

  EnterChildProcessJail();
  return Daemon::OnInit();
}

void MulticastProxy::Reset() {
  mdns_fwds_.clear();
  ssdp_fwds_.clear();
  bcast_fwds_.clear();
}

void MulticastProxy::OnParentProcessExit() {
  LOG(ERROR) << "Quitting because the parent process died";
  Reset();
  Quit();
}

void MulticastProxy::OnControlMessage(const SubprocessMessage& root_msg) {
  if (!root_msg.has_control_message()) {
    LOG(ERROR) << "Unexpected message type";
    return;
  }

  const ControlMessage& cm = root_msg.control_message();
  if (cm.has_mcast_control()) {
    ProcessMulticastForwardingControlMessage(cm.mcast_control());
  }
  if (cm.has_bcast_control()) {
    ProcessBroadcastForwardingControlMessage(cm.bcast_control());
  }
}

void MulticastProxy::ProcessMulticastForwardingControlMessage(
    const MulticastForwardingControlMessage& msg) {
  const std::string& lan_ifname = msg.lan_ifname();
  auto dir = msg.dir();
  bool inbound = dir == MulticastForwardingControlMessage::INBOUND_ONLY ||
                 dir == MulticastForwardingControlMessage::TWO_WAYS;
  bool outbound = dir == MulticastForwardingControlMessage::INBOUND_ONLY ||
                  dir == MulticastForwardingControlMessage::TWO_WAYS;

  MulticastForwarder::Direction mcast_fwd_dir;
  switch (dir) {
    case MulticastForwardingControlMessage::INBOUND_ONLY:
      mcast_fwd_dir = MulticastForwarder::Direction::kInboundOnly;
      break;
    case MulticastForwardingControlMessage::OUTBOUND_ONLY:
      mcast_fwd_dir = MulticastForwarder::Direction::kOutboundOnly;
      break;
    case MulticastForwardingControlMessage::TWO_WAYS:
      mcast_fwd_dir = MulticastForwarder::Direction::kTwoWays;
      break;
    default:
      LOG(ERROR) << "Unknown multicast forwarding direction: " << dir;
      return;
  }

  if (lan_ifname.empty()) {
    LOG(DFATAL)
        << "Received MulticastForwardingControlMessage w/ empty lan_ifname";
    return;
  }
  auto mdns_fwd = mdns_fwds_.find(lan_ifname);
  auto ssdp_fwd = ssdp_fwds_.find(lan_ifname);

  if (!msg.has_teardown()) {
    // Start multicast forwarders.
    if (mdns_fwd == mdns_fwds_.end()) {
      LOG(INFO) << "Enabling mDNS forwarding for device " << lan_ifname;
      auto fwd = std::make_unique<MulticastForwarder>(
          lan_ifname, kMdnsMcastAddress, kMdnsMcastAddress6, kMdnsPort);
      fwd->Init();
      mdns_fwd = mdns_fwds_.emplace(lan_ifname, std::move(fwd)).first;
    }
    if (outbound) {
      LOG(INFO) << "Starting forwarding outbound mDNS traffic between "
                << lan_ifname << " and " << msg.int_ifname();
    }
    if (inbound) {
      LOG(INFO) << "Starting forwarding inbound mDNS traffic between "
                << lan_ifname << " and " << msg.int_ifname();
    }
    if (!mdns_fwd->second->StartForwarding(msg.int_ifname(), mcast_fwd_dir)) {
      LOG(WARNING) << "mDNS forwarder could not be started between "
                   << lan_ifname << " and " << msg.int_ifname();
    }

    if (ssdp_fwd == ssdp_fwds_.end()) {
      LOG(INFO) << "Enabling SSDP forwarding for device " << lan_ifname;
      auto fwd = std::make_unique<MulticastForwarder>(
          lan_ifname, kSsdpMcastAddress, kSsdpMcastAddress6, kSsdpPort);
      fwd->Init();
      ssdp_fwd = ssdp_fwds_.emplace(lan_ifname, std::move(fwd)).first;
    }
    if (outbound) {
      LOG(INFO) << "Starting forwarding outbound SSDP traffic between "
                << lan_ifname << " and " << msg.int_ifname();
    }
    if (inbound) {
      LOG(INFO) << "Starting forwarding inbound SSDP traffic between "
                << lan_ifname << " and " << msg.int_ifname();
    }
    if (!ssdp_fwd->second->StartForwarding(msg.int_ifname(), mcast_fwd_dir)) {
      LOG(WARNING) << "SSDP forwarder could not be started on " << lan_ifname
                   << " and " << msg.int_ifname();
    }
    return;
  }

  // A bridge interface is removed.
  if (msg.has_int_ifname()) {
    if (mdns_fwd != mdns_fwds_.end()) {
      if (outbound) {
        LOG(INFO) << "Disabling forwarding outbound mDNS traffic between "
                  << lan_ifname << " and " << msg.int_ifname();
      }
      if (inbound) {
        LOG(INFO) << "Disabling forwarding inbound mDNS traffic between "
                  << lan_ifname << " and " << msg.int_ifname();
      }
      mdns_fwd->second->StopForwarding(msg.int_ifname(), mcast_fwd_dir);
    }
    if (ssdp_fwd != ssdp_fwds_.end()) {
      if (outbound) {
        LOG(INFO) << "Disabling forwarding outbound SSDP traffic between "
                  << lan_ifname << " and " << msg.int_ifname();
      }
      if (inbound) {
        LOG(INFO) << "Disabling forwarding inbound SSDP traffic between "
                  << lan_ifname << " and " << msg.int_ifname();
      }
      ssdp_fwd->second->StopForwarding(msg.int_ifname(), mcast_fwd_dir);
    }
    return;
  }

  // A physical interface is removed.
  if (mdns_fwd != mdns_fwds_.end()) {
    LOG(INFO) << "Disabling mDNS forwarding for physical interface "
              << lan_ifname;
    mdns_fwds_.erase(mdns_fwd);
  }
  if (ssdp_fwd != ssdp_fwds_.end()) {
    LOG(INFO) << "Disabling SSDP forwarding for physical interface "
              << lan_ifname;
    ssdp_fwds_.erase(ssdp_fwd);
  }
}

void MulticastProxy::ProcessBroadcastForwardingControlMessage(
    const BroadcastForwardingControlMessage& msg) {
  const std::string& lan_ifname = msg.lan_ifname();
  if (lan_ifname.empty()) {
    LOG(DFATAL)
        << "Received BroadcastForwardingControlMessage w/ empty lan_ifname";
    return;
  }
  auto bcast_fwd = bcast_fwds_.find(lan_ifname);

  if (!msg.has_teardown()) {
    if (bcast_fwd == bcast_fwds_.end()) {
      LOG(INFO) << "Enabling broadcast forwarding for device " << lan_ifname;
      auto fwd = std::make_unique<BroadcastForwarder>(lan_ifname);
      fwd->Init();
      bcast_fwd = bcast_fwds_.emplace(lan_ifname, std::move(fwd)).first;
    }
    LOG(INFO) << "Starting broadcast forwarding between " << lan_ifname
              << " and " << msg.int_ifname();
    if (!bcast_fwd->second->AddGuest(msg.int_ifname())) {
      LOG(WARNING) << "Broadcast forwarder could not be started on "
                   << lan_ifname << " and " << msg.int_ifname();
    }
    return;
  }

  // A bridge interface is removed.
  if (msg.has_int_ifname()) {
    if (bcast_fwd != bcast_fwds_.end()) {
      LOG(INFO) << "Disabling broadcast forwarding between " << lan_ifname
                << " and " << msg.int_ifname();
      bcast_fwd->second->RemoveGuest(msg.int_ifname());
    }
    return;
  }

  // A physical interface is removed.
  if (bcast_fwd != bcast_fwds_.end()) {
    LOG(INFO) << "Disabling broadcast forwarding for physical interface "
              << lan_ifname;
    bcast_fwds_.erase(bcast_fwd);
  }
}

}  // namespace patchpanel

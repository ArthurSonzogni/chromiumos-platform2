// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "patchpanel/connmark_updater.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/strcat.h>

namespace patchpanel {

namespace {
// Limit of how many pending UDP connections can be added to
// |pending_udp_connmark_operations_|.
constexpr int kPendingConnectionListLimit = 128;

// UDP protocol used to set protocol field in conntrack command.
constexpr char kProtocolUDP[] = "UDP";

// TCP protocol used to set protocol field in conntrack command.
constexpr char kProtocolTCP[] = "TCP";

// Types of conntrack events ConnmarkUpdater gets notified.
constexpr ConntrackMonitor::EventType kConntrackEvents[] = {
    ConntrackMonitor::EventType::kNew};

// Returns the "mark/mask" string for given mark and mask which can be
// used as an argument to call iptables, e.g., "0x00000040/0x000000e0".
std::string getFwmarkWithMask(const Fwmark mark, const Fwmark mask) {
  return base::StrCat({mark.ToString(), "/", mask.ToString()});
}
}  // namespace

ConnmarkUpdater::ConnmarkUpdater(ConntrackMonitor* monitor)
    : conntrack_monitor_(monitor) {
  process_runner_ = std::make_unique<MinijailedProcessRunner>();
  listener_ = conntrack_monitor_->AddListener(
      kConntrackEvents,
      base::BindRepeating(&ConnmarkUpdater::HandleConntrackEvent,
                          weak_factory_.GetWeakPtr()));
}

ConnmarkUpdater::ConnmarkUpdater(
    ConntrackMonitor* monitor,
    std::unique_ptr<MinijailedProcessRunner> process_runner)
    : conntrack_monitor_(monitor) {
  process_runner_ = std::move(process_runner);
  listener_ = conntrack_monitor_->AddListener(
      kConntrackEvents,
      base::BindRepeating(&ConnmarkUpdater::HandleConntrackEvent,
                          weak_factory_.GetWeakPtr()));
}

void ConnmarkUpdater::UpdateConnmark(
    const ConnmarkUpdater::Conntrack5Tuple& conn,
    const Fwmark mark,
    const Fwmark mask) {
  if (conn.src_addr.GetFamily() != conn.dst_addr.GetFamily()) {
    LOG(ERROR) << "The IP family of source address and destination address "
               << "of the conntrack tuple do not match.";
    return;
  }

  if (conn.proto == IPProtocol::kTCP) {
    // Update TCP connections directly only once because they are guaranteed
    // to be established on ARC side.
    if (!InvokeConntrack(conn, mark, mask)) {
      LOG(ERROR) << "Failed to update connmark for TCP connection.";
    }
    return;
  }

  // For UDP connections, adds to the pending list if update fails.
  if (InvokeConntrack(conn, mark, mask)) {
    return;
  }
  if (pending_udp_connmark_operations_.size() >= kPendingConnectionListLimit) {
    LOG(WARNING) << "Failed to add UDP connection to pending connection "
                    "list, reaching limit size.";
    return;
  }
  pending_udp_connmark_operations_.insert({conn, std::make_pair(mark, mask)});
}

void ConnmarkUpdater::HandleConntrackEvent(
    const ConntrackMonitor::Event& event) {
  // Currently we only cares about UDP connections, see more explanation in the
  // comment of |pending_udp_connmark_operations_|.
  if (event.proto != IPPROTO_UDP) {
    return;
  }
  Conntrack5Tuple conn = {.src_addr = event.src,
                          .dst_addr = event.dst,
                          .sport = event.sport,
                          .dport = event.dport,
                          .proto = IPProtocol::kUDP};
  // Find the connection in |pending_udp_connmark_operations_|, if it is in the
  // list, try updating connmark and delete the connection from the list.
  auto it = pending_udp_connmark_operations_.find(conn);
  if (it == pending_udp_connmark_operations_.end()) {
    return;
  }
  const auto [mark, mask] = it->second;
  if (!InvokeConntrack(conn, mark, mask)) {
    LOG(ERROR) << "Updating connmark failed, deleting connection from pending "
                  "connection list.";
  }
  // Whether the update succeeded or not, there would not be another conntrack
  // event to trigger update, delete the connection from pending list.
  pending_udp_connmark_operations_.erase(it);
}

bool ConnmarkUpdater::InvokeConntrack(const Conntrack5Tuple& conn,
                                      const Fwmark mark,
                                      const Fwmark mask) {
  std::string proto;
  switch (conn.proto) {
    case IPProtocol::kTCP:
      proto = kProtocolTCP;
      break;
    case IPProtocol::kUDP:
      proto = kProtocolUDP;
      break;
  }
  std::vector<std::string> args = {"-p",      proto,
                                   "-s",      conn.src_addr.ToString(),
                                   "-d",      conn.dst_addr.ToString(),
                                   "--sport", std::to_string(conn.sport),
                                   "--dport", std::to_string(conn.dport),
                                   "-m",      getFwmarkWithMask(mark, mask)};
  return process_runner_->conntrack("-U", args) == 0;
}

size_t ConnmarkUpdater::GetPendingListSizeForTesting() {
  return pending_udp_connmark_operations_.size();
}

}  // namespace patchpanel

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/kills_server.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "vm_tools/concierge/byte_unit.h"

using vm_tools::vm_memory_management::DecisionLatency;
using vm_tools::vm_memory_management::KillDecisionRequest;
using vm_tools::vm_memory_management::KillDecisionResponse;

namespace vm_tools::concierge::mm {

KillsServer::KillsServer(int port, SocketFactory socket_factory)
    : Server(port, socket_factory) {}

void KillsServer::SetKillRequestHandler(KillRequestHandler callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  kill_request_handler_ = callback;
}

void KillsServer::SetNoKillCandidateNotification(
    NoKillCandidateNotification callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  no_kill_candiate_callback_ = callback;
}

void KillsServer::SetDecisionLatencyNotification(
    DecisionLatencyNotification callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  decision_latency_callback_ = callback;
}

const KillsServer::KillRequestHandler& KillsServer::GetKillRequestHandler() {
  return kill_request_handler_;
}

const KillsServer::NoKillCandidateNotification&
KillsServer::GetNoKillCandidateCallback() {
  return no_kill_candiate_callback_;
}

void KillsServer::HandlePacket(
    const Connection& connection,
    const VmMemoryManagementPacket& received_packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (received_packet.type()) {
    case PacketType::PACKET_TYPE_KILL_REQUEST:
      return HandleKillRequest(connection, received_packet);
    case PacketType::PACKET_TYPE_NO_KILL_CANDIDATES:
      return HandleNoKillCandidates(connection, received_packet);
    case PacketType::PACKET_TYPE_DECISION_LATENCY:
      return HandleDecisionLatency(connection, received_packet);
    default:
      LOG(ERROR) << "Unknown command received from client: "
                 << connection.client.cid
                 << " cmd_id: " << static_cast<size_t>(received_packet.type());
  }
}

void KillsServer::HandleKillRequest(const Connection& connection,
                                    const VmMemoryManagementPacket& packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!packet.has_kill_decision_request()) {
    LOG(ERROR) << "Received malformed kill decision request from VM CID: "
               << connection.client.cid;
    return;
  }

  const KillDecisionRequest& request = packet.kill_decision_request();

  // Process size is sent as KB units.
  size_t proc_size = KiB(request.size_kb());

  size_t freed_space = 0;

  if (kill_request_handler_) {
    freed_space = kill_request_handler_.Run(connection.client, proc_size,
                                            request.priority());
  }

  // Client expects a response in KB units.
  uint32_t freed_space_kb = (freed_space / KiB(1));

  VmMemoryManagementPacket reply_packet;
  reply_packet.set_type(PacketType::PACKET_TYPE_KILL_DECISION);

  KillDecisionResponse* kill_decision =
      reply_packet.mutable_kill_decision_response();
  kill_decision->set_sequence_num(request.sequence_num());
  kill_decision->set_size_freed_kb(freed_space_kb);

  if (!connection.socket->WritePacket(reply_packet)) {
    LOG(ERROR) << "Failed to write kill decision response.";
    RemoveConnection(connection.client.connection_id);
  }
}

void KillsServer::HandleNoKillCandidates(
    const Connection& connection, const VmMemoryManagementPacket&) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  no_kill_candiate_callback_.Run(connection.client);
}

void KillsServer::HandleDecisionLatency(
    const Connection& connection,
    const VmMemoryManagementPacket& packet) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!packet.has_decision_latency()) {
    LOG(ERROR) << "Received malformed decision latency packet from VM CID: "
               << connection.client.cid;
    return;
  }

  decision_latency_callback_.Run(connection.client, packet.decision_latency());
}

}  // namespace vm_tools::concierge::mm

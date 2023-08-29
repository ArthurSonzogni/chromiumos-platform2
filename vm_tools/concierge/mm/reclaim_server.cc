// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/reclaim_server.h"

#include <memory>
#include <utility>

#include <base/logging.h>

using vm_tools::vm_memory_management::MglruResponse;

namespace vm_tools::concierge::mm {

ReclaimServer::ReclaimServer(int port, SocketFactory socket_factory)
    : Server(port, socket_factory) {}

std::optional<MglruStats> ReclaimServer::GetMglruStats(int cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const Connection* connection =
      GetConnection(cid, ConnectionType::CONNECTION_TYPE_STATS);

  if (!connection) {
    LOG(ERROR) << "No valid client to handle stats request";
    return std::nullopt;
  }

  if (!SendNoPayloadPacket(*connection,
                           PacketType::PACKET_TYPE_MGLRU_REQUEST)) {
    LOG(ERROR) << "Failed to send MGLRU stats request to client: "
               << connection->client.cid;
    RemoveConnection(connection->client.connection_id);
    return std::nullopt;
  }

  VmMemoryManagementPacket response;

  if (!connection->socket->ReadPacket(response)) {
    LOG(ERROR) << "Failed to read MGLRU response packet from client";
    RemoveConnection(connection->client.connection_id);
    return std::nullopt;
  }

  if (response.type() != PacketType::PACKET_TYPE_MGLRU_RESPONSE ||
      !response.has_mglru_response()) {
    LOG(ERROR) << "Received invalid response to MGLRU stats request";
    return std::nullopt;
  }

  return response.mglru_response().stats();
}

void ReclaimServer::SetNewGenerationNotification(
    NewGenerationNotification callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  new_generation_callback_ = callback;
}

void ReclaimServer::HandlePacket(
    const Connection& connection,
    const VmMemoryManagementPacket& received_packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  switch (received_packet.type()) {
    case PacketType::PACKET_TYPE_MGLRU_RESPONSE:
      return HandleMglruResponse(connection, received_packet);
    default:
      LOG(ERROR) << "Unknown command received from client: "
                 << connection.client.cid
                 << " cmd_id: " << static_cast<size_t>(received_packet.type());
  }
}

void ReclaimServer::HandleMglruResponse(
    const Connection& connection,
    const VmMemoryManagementPacket& packet) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!packet.has_mglru_response()) {
    LOG(ERROR) << "Received malformed MGLRU response packet from VM CID: "
               << connection.client.cid;
    return;
  }

  new_generation_callback_.Run(connection.client.cid,
                               packet.mglru_response().stats());
}

}  // namespace vm_tools::concierge::mm

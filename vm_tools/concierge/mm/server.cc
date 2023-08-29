// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/server.h"

#include <sys/socket.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <memory>
#include <utility>

#include <base/logging.h>

#include "vm_tools/concierge/mm/mm_service.h"
#include "vm_tools/concierge/mm/vm_socket.h"

using vm_tools::vm_memory_management::ConnectionHandshake;

namespace vm_tools::concierge::mm {

Server::Server(int port, SocketFactory socket_factory)
    : port_(port), socket_factory_(socket_factory) {}

bool Server::StartListening() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (server_socket_) {
    LOG(ERROR) << "Server is already listening.";
    return false;
  }

  // Host is always allowed to connect.
  RegisterVm(VMADDR_CID_LOCAL);

  // Create a new server socket.
  server_socket_ = socket_factory_.Run({});

  // The necessary backlog size depends on how many VMs are managed by the
  // service.
  if (!server_socket_->Listen(port_, MmService::ManagedVms().size())) {
    PLOG(ERROR) << "Failed to start listening for a connection on "
                   "Server VSOCK";
    return false;
  }

  if (!server_socket_->OnReadable(
          base::BindRepeating(&Server::HandleAccept, base::Unretained(this)))) {
    PLOG(ERROR) << "Failed to watch for connections on VSOCK.";
    return false;
  }

  LOG(INFO) << "Waiting for Server socket connections on "
               "VSOCK port: "
            << port_;

  return true;
}

void Server::RegisterVm(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  allowed_vms_.emplace(vm_cid);
}

void Server::RemoveVm(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<int> connections_ids_to_erase{};

  for (const auto& connection : connections_) {
    if (vm_cid == connection.second.client.cid) {
      client_disconnected_callback_.Run(connection.second.client);
      connections_ids_to_erase.emplace_back(connection.first);
    }
  }

  for (int connection_id : connections_ids_to_erase) {
    connections_.erase(connection_id);
  }

  allowed_vms_.erase(vm_cid);
}

void Server::SetClientConnectionNotification(
    ClientConnectionNotification callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  client_connection_callback_ = callback;
}

void Server::SetClientDisconnectedNotification(
    ClientDisconnectedNotification callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  client_disconnected_callback_ = callback;
}

// static
std::unique_ptr<VmSocket> Server::SocketFactoryImpl(base::ScopedFD fd) {
  return std::make_unique<VmSocket>(std::move(fd));
}

bool Server::SendNoPayloadPacket(const Connection& connection,
                                 PacketType type) {
  VmMemoryManagementPacket packet;
  packet.set_type(type);
  return connection.socket->WritePacket(packet);
}

const Connection* Server::GetConnection(int cid,
                                        ConnectionType connection_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& connection : connections_) {
    if (connection.second.client.cid == cid &&
        connection.second.client.type == connection_type) {
      return &connections_[connection.second.client.connection_id];
    }
  }

  return nullptr;
}

void Server::RemoveConnection(int connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connections_.contains(connection_id)) {
    return;
  }

  client_disconnected_callback_.Run(connections_[connection_id].client);
  connections_.erase(connection_id);
}

void Server::HandleAccept() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (connections_.size() >= kMaxConnections) {
    LOG(ERROR) << "Max connections reached. Ignoring new connection request.";
    return;
  }

  Connection new_connection;

  int connected_cid = 0;

  base::ScopedFD accepted_fd = server_socket_->Accept(connected_cid);

  // Use the accepted_fd as the ID for this connection. ids are not used as fds,
  // but the fd is a unique identifier within the scope of this server and can
  // be used as the id value.
  int new_connection_id = accepted_fd.get();

  new_connection.socket = socket_factory_.Run(std::move(accepted_fd));

  if (!new_connection.socket->IsValid()) {
    PLOG(ERROR) << "Server failed to accept new connection";
    return;
  }

  if (!allowed_vms_.contains(connected_cid)) {
    LOG(ERROR) << "Server rejecting connection from "
                  "un-registered VM: "
               << connected_cid;
    return;
  }

  if (!new_connection.socket->OnReadable(
          base::BindRepeating(&Server::HandleConnectionReadable,
                              base::Unretained(this), new_connection_id))) {
    PLOG(ERROR) << "Failed to start watching reads from new client";
    return;
  }

  new_connection.client.cid = connected_cid;
  new_connection.client.connection_id = new_connection_id;

  connections_.emplace(new_connection_id, std::move(new_connection));
}

void Server::HandleConnectionReadable(int connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connections_.contains(connection_id)) {
    LOG(ERROR) << "Received request from non-existant client";
    return;
  }

  Connection& connection = connections_[connection_id];

  VmMemoryManagementPacket received_packet;

  if (!connection.socket->ReadPacket(received_packet)) {
    LOG(ERROR) << "Failed to fully read packet from client: "
               << connection.client.cid;
    RemoveConnection(connection_id);
    return;
  }

  switch (received_packet.type()) {
    case PacketType::PACKET_TYPE_HANDSHAKE:
      return HandleConnectionHandshake(connection, received_packet);
    default:
      return HandlePacket(connection, received_packet);
  }
}

void Server::HandleConnectionHandshake(Connection& connection,
                                       const VmMemoryManagementPacket& packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!packet.has_handshake() ||
      packet.handshake().type() ==
          ConnectionType::CONNECTION_TYPE_UNSPECIFIED) {
    LOG(ERROR) << "Invalid connection handshake received";
    if (!SendNoPayloadPacket(connection,
                             PacketType::PACKET_TYPE_CONNECTION_NACK)) {
      RemoveConnection(connection.client.connection_id);
    }
    return;
  }

  const ConnectionHandshake& handshake = packet.handshake();

  connection.client.type = handshake.type();

  if (!SendNoPayloadPacket(connection,
                           PacketType::PACKET_TYPE_CONNECTION_ACK)) {
    LOG(ERROR) << "Failed to send CONNECTION_ACK to client: "
               << connection.client.cid;
    RemoveConnection(connection.client.connection_id);
    return;
  }

  client_connection_callback_.Run(connection.client);

  LOG(INFO) << "Server accepted new connection. CID: " << connection.client.cid
            << " Type: " << ConnectionType_Name(connection.client.type);
}

}  // namespace vm_tools::concierge::mm

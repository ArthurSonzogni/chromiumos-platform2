// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_SERVER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include <base/functional/callback.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/mm/vm_socket.h"

using vm_tools::vm_memory_management::ConnectionType;
using vm_tools::vm_memory_management::PacketType;
using vm_tools::vm_memory_management::VmMemoryManagementPacket;

namespace vm_tools::concierge::mm {

struct Client {
  // The CID of the client. VMADDR_CID_LOCAL for host clients, and a valid VM
  // CID for VM clients. Initialized to -2 which is an invalid CID.
  int cid = -2;

  // The connection id of the client. This ID is unique in the scope of this
  // server and assigned when a client connects. It is necessary because a given
  // context can have more than one client. Initialized to -1 which is an
  // invalid connection_id.
  int connection_id = -1;

  // The type of the connection.
  ConnectionType type = ConnectionType::CONNECTION_TYPE_UNSPECIFIED;
};

// Represents a connection to the server.
struct Connection {
  // The specific client that is connected.
  Client client{};

  // The socket open to the connection.
  std::unique_ptr<VmSocket> socket{};
};

// The Server interface is a server that listens on the
// supplied socket for connections from Clients. The
// connection handshakes and packet transport are all handled by the base class.
// Implementations can implement handling logic for specific message types.
class Server {
 public:
  using SocketFactory =
      base::RepeatingCallback<std::unique_ptr<VmSocket>(base::ScopedFD)>;

  explicit Server(int port, SocketFactory socket_factory);

  virtual ~Server() = default;

  // Starts listening for connections on the specified port.
  bool StartListening();

  // Registers a VM with the server. Only connections from registered VMs will
  // be accepted.
  void RegisterVm(int vm_cid);

  // Perform cleanup operations for a VM when it is shutting down.
  void RemoveVm(int vm_cid);

  // START: Event callbacks.

  // Sets the callback to be run when a new client connects to the server.
  using ClientConnectionNotification = base::RepeatingCallback<void(Client)>;
  void SetClientConnectionNotification(ClientConnectionNotification callback);

  // Sets the callback to be run when a new client disconnects from the
  // server.
  using ClientDisconnectedNotification = base::RepeatingCallback<void(Client)>;
  void SetClientDisconnectedNotification(
      ClientDisconnectedNotification callback);

  // END: Event Callbacks.
 protected:
  // Creates socket instances for use by the server.
  static std::unique_ptr<VmSocket> SocketFactoryImpl(base::ScopedFD fd);

  // Sends a packet with the specified type and no payload to the specified
  // connection.
  static bool SendNoPayloadPacket(const Connection& connection,
                                  PacketType type);

  // Gets a connection matching the specified parameters, or nullptr if none
  // exists.
  const Connection* GetConnection(int cid, ConnectionType type);

  // Removes the specified connection from the server.
  void RemoveConnection(int connection_id);

  // Returns the ClientConnectionNotification for this server.
  const ClientConnectionNotification& GetClientConnectionCallback();

  // Returns the ClientDisconnectedNotification for this server.
  const ClientDisconnectedNotification& GetClientDisconnectedCallback();

  // Performs the necessary actions for the received packet.
  virtual void HandlePacket(
      const Connection& connection,
      const VmMemoryManagementPacket& received_packet) = 0;

  // Ensure calls are made on the correct sequence.
  SEQUENCE_CHECKER(sequence_checker_);

 private:
  // Handles accepting a new connection.
  void HandleAccept();

  // Handles reading from a client connection.
  void HandleConnectionReadable(int connection_id);

  // Handles performing the initial connection handshake.
  void HandleConnectionHandshake(Connection& connection,
                                 const VmMemoryManagementPacket& packet);

  // The port on which this server listens.
  const int port_;

  // Creates socket instances.
  const SocketFactory socket_factory_;

  // The socket used to accept connections.
  std::unique_ptr<VmSocket> server_socket_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // Contains the VM CIDs that the server is allowed to accept a connection
  // from.
  base::flat_set<int> allowed_vms_ GUARDED_BY_CONTEXT(sequence_checker_){};

  // The maximum number of simultaneous connections that are allowed on one
  // server at a given time.
  static constexpr size_t kMaxConnections = 8;

  base::flat_map<int /* connection_id */, Connection> connections_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // Event Callbacks.
  ClientConnectionNotification client_connection_callback_
      GUARDED_BY_CONTEXT(sequence_checker_) = base::DoNothing();
  ClientDisconnectedNotification client_disconnected_callback_
      GUARDED_BY_CONTEXT(sequence_checker_) = base::DoNothing();
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_SERVER_H_

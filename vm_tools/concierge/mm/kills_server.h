// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_KILLS_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_KILLS_SERVER_H_

#include <memory>
#include <vector>

#include "vm_tools/concierge/mm/server.h"

using vm_tools::vm_memory_management::DecisionLatency;
using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// The KillsServer accepts and handles low memory kill related
// messages for the VM Memory Management Service.
class KillsServer : public Server {
 public:
  explicit KillsServer(int port,
                       SocketFactory socket_factory =
                           base::BindRepeating(&Server::SocketFactoryImpl));

  KillsServer(const KillsServer&) = delete;
  KillsServer& operator=(const KillsServer&) = delete;

  // START: Event callbacks.

  // Sets the callback that handles and makes a decision about a kill
  // request.
  using KillRequestHandler =
      base::RepeatingCallback<size_t(Client, size_t, ResizePriority)>;
  void SetKillRequestHandler(KillRequestHandler callback);

  // Sets the callback to be run when a client indicates is has no kill
  // candidates.
  using NoKillCandidateNotification = base::RepeatingCallback<void(Client)>;
  void SetNoKillCandidateNotification(NoKillCandidateNotification callback);

  // Sets the callback to be run when a decision latency packet is received.
  using DecisionLatencyNotification =
      base::RepeatingCallback<void(Client, const DecisionLatency&)>;
  void SetDecisionLatencyNotification(DecisionLatencyNotification callback);

  // END: Event Callbacks.
 protected:
  // Gets the kill request handler for this server.
  const KillRequestHandler& GetKillRequestHandler();

  // Gets the no kill candidates callback for this server.
  const NoKillCandidateNotification& GetNoKillCandidateCallback();

 private:
  // Performs implementation specific actions based on the received packet.
  void HandlePacket(const Connection& connection,
                    const VmMemoryManagementPacket& received_packet) override;

  // Handles a kill request from a client.
  void HandleKillRequest(const Connection& connection,
                         const VmMemoryManagementPacket& packet);

  // Handles a no kill candidate event from a client.
  void HandleNoKillCandidates(const Connection& connection,
                              const VmMemoryManagementPacket& packet) const;

  // Handles a decision latency message from a client.
  void HandleDecisionLatency(const Connection& connection,
                             const VmMemoryManagementPacket& packet) const;

  // Event Callbacks.
  KillRequestHandler kill_request_handler_
      GUARDED_BY_CONTEXT(sequence_checker_){};
  NoKillCandidateNotification no_kill_candiate_callback_
      GUARDED_BY_CONTEXT(sequence_checker_) = base::DoNothing();
  DecisionLatencyNotification decision_latency_callback_
      GUARDED_BY_CONTEXT(sequence_checker_) = base::DoNothing();
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_KILLS_SERVER_H_

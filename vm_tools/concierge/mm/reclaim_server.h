// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_RECLAIM_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_RECLAIM_SERVER_H_

#include <memory>
#include <vector>

#include "vm_tools/concierge/mm/server.h"

namespace vm_tools::concierge::mm {

using vm_tools::vm_memory_management::MglruStats;

// The ReclaimServer accepts and handles MGLRU stats related
// messages for the VM Memory Management Service.
class ReclaimServer : public Server {
 public:
  explicit ReclaimServer(int port,
                         SocketFactory socket_factory =
                             base::BindRepeating(&Server::SocketFactoryImpl));

  ReclaimServer(const ReclaimServer&) = delete;
  ReclaimServer& operator=(const ReclaimServer&) = delete;

  // Retrieves the MGLRU stats of the specified context.
  virtual std::optional<MglruStats> GetMglruStats(int cid);

  // START: Event callbacks.

  // Sets the callback to be run when a client sends a new MGLRU generation
  // event.
  using NewGenerationNotification =
      base::RepeatingCallback<void(int, MglruStats)>;
  void SetNewGenerationNotification(NewGenerationNotification callback);

  // END: Event Callbacks.
 protected:
  // Get the new generation callback for this server.
  const NewGenerationNotification& GetNewGenerationCallback();

 private:
  // Performs implementation specific actions based on the received packet.
  void HandlePacket(const Connection& connection,
                    const VmMemoryManagementPacket& received_packet) override;

  // Handles a MGLRU response from a client.
  void HandleMglruResponse(const Connection& connection,
                           const VmMemoryManagementPacket& packet) const;

  // Event Callbacks.
  NewGenerationNotification new_generation_callback_
      GUARDED_BY_CONTEXT(sequence_checker_) = base::DoNothing();
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_RECLAIM_SERVER_H_

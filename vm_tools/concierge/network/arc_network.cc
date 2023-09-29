// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/arc_network.h"

#include <optional>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"

#include "vm_tools/concierge/network/scoped_network.h"

namespace vm_tools::concierge {

// static
std::unique_ptr<ArcNetwork> ArcNetwork::Create(scoped_refptr<dbus::Bus> bus,
                                               uint32_t vsock_cid) {
  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";
    return nullptr;
  }

  std::optional<patchpanel::Client::ArcVMAllocation> allocation =
      network_client->NotifyArcVmStartup(vsock_cid);
  if (!allocation.has_value()) {
    LOG(ERROR) << "No network devices available";
    return nullptr;
  }

  return base::WrapUnique(new ArcNetwork(std::move(network_client),
                                         std::move(*allocation), vsock_cid));
}

ArcNetwork::~ArcNetwork() {
  if (!client().NotifyArcVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Failed to notify patchpanel for shutdown of Arc, cid="
                 << vsock_cid_;
  }
}
ArcNetwork::ArcNetwork(std::unique_ptr<patchpanel::Client> client,
                       patchpanel::Client::ArcVMAllocation allocation,
                       uint32_t vsock_cid)
    : ScopedNetwork(std::move(client)),
      allocation_(std::move(allocation)),
      vsock_cid_(vsock_cid) {}

}  // namespace vm_tools::concierge

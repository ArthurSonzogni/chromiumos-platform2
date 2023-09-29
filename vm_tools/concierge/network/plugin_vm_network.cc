// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/plugin_vm_network.h"

#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"

#include "vm_tools/concierge/network/scoped_network.h"

namespace vm_tools::concierge {

// static
std::unique_ptr<PluginVmNetwork> PluginVmNetwork::Create(
    scoped_refptr<dbus::Bus> bus, const VmId& id, int subnet_index) {
  // Generate a unique ID for patchpanel to associate with this network.
  std::ostringstream oss;
  oss << id;
  std::size_t id_hash = std::hash<std::string>{}(oss.str());

  // Get a dbus handle to patchpanel.
  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";
    return nullptr;
  }

  // Allocate network resources for this VM.
  auto network_alloc =
      network_client->NotifyParallelsVmStartup(id_hash, subnet_index);
  if (!network_alloc.has_value()) {
    LOG(ERROR) << "No network allocation available from patchpanel";
    return nullptr;
  }

  return base::WrapUnique(new PluginVmNetwork(
      std::move(network_client), std::move(*network_alloc), id_hash));
}

PluginVmNetwork::~PluginVmNetwork() {
  if (!client().NotifyParallelsVmShutdown(id_)) {
    LOG(WARNING) << "Unable to notify networking services for Parallels exit";
  }
}

PluginVmNetwork::PluginVmNetwork(
    std::unique_ptr<patchpanel::Client> client,
    patchpanel::Client::ParallelsAllocation allocation,
    uint64_t id)
    : ScopedNetwork(std::move(client)),
      allocation_(std::move(allocation)),
      id_(id) {}

}  // namespace vm_tools::concierge

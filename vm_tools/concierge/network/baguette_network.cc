// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/baguette_network.h"

#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "chromeos/patchpanel/dbus/client.h"
#include "vm_tools/concierge/network/guest_os_network.h"

namespace vm_tools::concierge {

// static
std::unique_ptr<BaguetteNetwork> BaguetteNetwork::Create(
    scoped_refptr<dbus::Bus> bus, uint32_t vsock_cid) {
  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";
    return nullptr;
  }

  std::optional<patchpanel::Client::TerminaAllocation> allocation =
      network_client->NotifyTerminaVmStartup(vsock_cid);
  if (!allocation.has_value()) {
    LOG(ERROR) << "No network devices available";
    return nullptr;
  }

  return base::WrapUnique(new BaguetteNetwork(
      std::move(network_client), std::move(*allocation), vsock_cid));
}

BaguetteNetwork::~BaguetteNetwork() {
  if (!client().NotifyTerminaVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Failed to notify patchpanel for shutdown of Baguette, cid="
                 << vsock_cid_;
  }
}

std::string BaguetteNetwork::TapDevice() const {
  return allocation_.tap_device_ifname;
}

net_base::IPv4Address BaguetteNetwork::AddressV4() const {
  return allocation_.termina_ipv4_address;
}

net_base::IPv4Address BaguetteNetwork::GatewayV4() const {
  return allocation_.gateway_ipv4_address;
}

net_base::IPv4CIDR BaguetteNetwork::SubnetV4() const {
  return allocation_.termina_ipv4_subnet;
}

net_base::IPv4Address BaguetteNetwork::ContainerAddressV4() const {
  CHECK(false) << "Baguette has no container";
  return {};
}

net_base::IPv4CIDR BaguetteNetwork::ContainerSubnetV4() const {
  CHECK(false) << "Baguette has no container";
  return {};
}

BaguetteNetwork::BaguetteNetwork(
    std::unique_ptr<patchpanel::Client> client,
    patchpanel::Client::TerminaAllocation allocation,
    uint32_t vsock_cid)
    : GuestOsNetwork(std::move(client), vsock_cid),
      allocation_(std::move(allocation)) {}

}  // namespace vm_tools::concierge

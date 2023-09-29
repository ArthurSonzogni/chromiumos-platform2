// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/termina_network.h"

#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "chromeos/patchpanel/dbus/client.h"

#include "vm_tools/concierge/network/guest_os_network.h"

namespace vm_tools::concierge {

// static
std::unique_ptr<TerminaNetwork> TerminaNetwork::Create(
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

  return base::WrapUnique(new TerminaNetwork(
      std::move(network_client), std::move(*allocation), vsock_cid));
}

TerminaNetwork::~TerminaNetwork() {
  if (!client().NotifyTerminaVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Failed to notify patchpanel for shutdown of Termina, cid="
                 << vsock_cid_;
  }
}

std::string TerminaNetwork::TapDevice() const {
  return allocation_.tap_device_ifname;
}

net_base::IPv4Address TerminaNetwork::AddressV4() const {
  return allocation_.termina_ipv4_address;
}

net_base::IPv4Address TerminaNetwork::GatewayV4() const {
  return allocation_.gateway_ipv4_address;
}

net_base::IPv4CIDR TerminaNetwork::SubnetV4() const {
  return allocation_.termina_ipv4_subnet;
}

net_base::IPv4Address TerminaNetwork::ContainerAddressV4() const {
  return allocation_.container_ipv4_address;
}

net_base::IPv4CIDR TerminaNetwork::ContainerSubnetV4() const {
  return allocation_.container_ipv4_subnet;
}

TerminaNetwork::TerminaNetwork(std::unique_ptr<patchpanel::Client> client,
                               patchpanel::Client::TerminaAllocation allocation,
                               uint32_t vsock_cid)
    : GuestOsNetwork(std::move(client), vsock_cid),
      allocation_(std::move(allocation)) {}

}  // namespace vm_tools::concierge

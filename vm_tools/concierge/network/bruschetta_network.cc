// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/bruschetta_network.h"

#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "chromeos/patchpanel/dbus/client.h"
#include "vm_tools/concierge/network/guest_os_network.h"

namespace vm_tools::concierge {

// static
std::unique_ptr<BruschettaNetwork> BruschettaNetwork::Create(
    scoped_refptr<dbus::Bus> bus, uint32_t vsock_cid) {
  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";
    return nullptr;
  }

  std::optional<patchpanel::Client::BruschettaAllocation> allocation =
      network_client->NotifyBruschettaVmStartup(vsock_cid);
  if (!allocation.has_value()) {
    LOG(ERROR) << "No network devices available";
    return nullptr;
  }

  return base::WrapUnique(new BruschettaNetwork(
      std::move(network_client), std::move(*allocation), vsock_cid));
}

BruschettaNetwork::~BruschettaNetwork() {
  if (!client().NotifyBruschettaVmShutdown(vsock_cid_)) {
    LOG(WARNING)
        << "Failed to notify patchpanel for shutdown of Bruschetta, cid="
        << vsock_cid_;
  }
}

std::string BruschettaNetwork::TapDevice() const {
  return allocation_.tap_device_ifname;
}

net_base::IPv4Address BruschettaNetwork::AddressV4() const {
  return allocation_.bruschetta_ipv4_address;
}

net_base::IPv4Address BruschettaNetwork::GatewayV4() const {
  return allocation_.gateway_ipv4_address;
}

net_base::IPv4CIDR BruschettaNetwork::SubnetV4() const {
  return allocation_.bruschetta_ipv4_subnet;
}

net_base::IPv4Address BruschettaNetwork::ContainerAddressV4() const {
  CHECK(false) << "Bruschetta has no container";
  return {};
}

net_base::IPv4CIDR BruschettaNetwork::ContainerSubnetV4() const {
  CHECK(false) << "Bruschetta has no container";
  return {};
}

BruschettaNetwork::BruschettaNetwork(
    std::unique_ptr<patchpanel::Client> client,
    patchpanel::Client::BruschettaAllocation allocation,
    uint32_t vsock_cid)
    : GuestOsNetwork(std::move(client), vsock_cid),
      allocation_(std::move(allocation)) {}

}  // namespace vm_tools::concierge

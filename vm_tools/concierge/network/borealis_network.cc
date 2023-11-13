// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/borealis_network.h"

#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "chromeos/patchpanel/dbus/client.h"
#include "vm_tools/concierge/network/guest_os_network.h"

namespace vm_tools::concierge {

// static
std::unique_ptr<BorealisNetwork> BorealisNetwork::Create(
    scoped_refptr<dbus::Bus> bus, uint32_t vsock_cid) {
  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";
    return nullptr;
  }

  std::optional<patchpanel::Client::BorealisAllocation> allocation =
      network_client->NotifyBorealisVmStartup(vsock_cid);
  if (!allocation.has_value()) {
    LOG(ERROR) << "No network devices available";
    return nullptr;
  }

  return base::WrapUnique(new BorealisNetwork(
      std::move(network_client), std::move(*allocation), vsock_cid));
}

BorealisNetwork::~BorealisNetwork() {
  if (!client().NotifyBorealisVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Failed to notify patchpanel for shutdown of Borealis, cid="
                 << vsock_cid_;
  }
}

std::string BorealisNetwork::TapDevice() const {
  return allocation_.tap_device_ifname;
}

net_base::IPv4Address BorealisNetwork::AddressV4() const {
  return allocation_.borealis_ipv4_address;
}

net_base::IPv4Address BorealisNetwork::GatewayV4() const {
  return allocation_.gateway_ipv4_address;
}

net_base::IPv4CIDR BorealisNetwork::SubnetV4() const {
  return allocation_.borealis_ipv4_subnet;
}

net_base::IPv4Address BorealisNetwork::ContainerAddressV4() const {
  CHECK(false) << "Borealis has no container";
  return {};
}

net_base::IPv4CIDR BorealisNetwork::ContainerSubnetV4() const {
  CHECK(false) << "Borealis has no container";
  return {};
}

BorealisNetwork::BorealisNetwork(
    std::unique_ptr<patchpanel::Client> client,
    patchpanel::Client::BorealisAllocation allocation,
    uint32_t vsock_cid)
    : GuestOsNetwork(std::move(client), vsock_cid),
      allocation_(std::move(allocation)) {}

}  // namespace vm_tools::concierge

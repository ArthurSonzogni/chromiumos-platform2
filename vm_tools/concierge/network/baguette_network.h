// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_NETWORK_BAGUETTE_NETWORK_H_
#define VM_TOOLS_CONCIERGE_NETWORK_BAGUETTE_NETWORK_H_

#include <memory>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "chromeos/patchpanel/dbus/client.h"
#include "vm_tools/concierge/network/guest_os_network.h"

namespace vm_tools::concierge {

class BaguetteNetwork : public GuestOsNetwork {
 public:
  static std::unique_ptr<BaguetteNetwork> Create(scoped_refptr<dbus::Bus> bus,
                                                 uint32_t vsock_cid);

  ~BaguetteNetwork() override;

  std::string TapDevice() const override;
  net_base::IPv4Address AddressV4() const override;
  net_base::IPv4Address GatewayV4() const override;
  net_base::IPv4CIDR SubnetV4() const override;
  net_base::IPv4Address ContainerAddressV4() const override;
  net_base::IPv4CIDR ContainerSubnetV4() const override;

 private:
  // Baguette re-uses Termina allocation
  BaguetteNetwork(std::unique_ptr<patchpanel::Client> client,
                  patchpanel::Client::TerminaAllocation allocation,
                  uint32_t vsock_cid);

  patchpanel::Client::TerminaAllocation allocation_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_NETWORK_BAGUETTE_NETWORK_H_

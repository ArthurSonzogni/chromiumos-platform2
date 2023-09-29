// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_NETWORK_GUEST_OS_NETWORK_H_
#define VM_TOOLS_CONCIERGE_NETWORK_GUEST_OS_NETWORK_H_

#include <memory>
#include <string>

#include "net-base/ipv4_address.h"

#include "vm_tools/concierge/network/scoped_network.h"

namespace vm_tools::concierge {

// Parent class for GuestOS's linux-like VMs (e.g. termina, bruchetta and
// borealis). Those classes all use TerminaVm and provide different child
// implementations for networking (e.g. TerminaNetwork).
class GuestOsNetwork : public ScopedNetwork {
 public:
  virtual std::string TapDevice() const = 0;
  virtual net_base::IPv4Address AddressV4() const = 0;
  virtual net_base::IPv4Address GatewayV4() const = 0;
  virtual net_base::IPv4CIDR SubnetV4() const = 0;
  virtual net_base::IPv4Address ContainerAddressV4() const = 0;
  virtual net_base::IPv4CIDR ContainerSubnetV4() const = 0;

 protected:
  GuestOsNetwork(std::unique_ptr<patchpanel::Client> client,
                 uint32_t vsock_cid);

  uint32_t vsock_cid_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_NETWORK_GUEST_OS_NETWORK_H_

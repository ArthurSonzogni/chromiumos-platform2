// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_NETWORK_ARC_NETWORK_H_
#define VM_TOOLS_CONCIERGE_NETWORK_ARC_NETWORK_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "chromeos/patchpanel/dbus/client.h"

#include "vm_tools/concierge/network/scoped_network.h"

namespace dbus {
class Bus;
}

namespace vm_tools::concierge {

class ArcNetwork : public ScopedNetwork {
 public:
  static std::unique_ptr<ArcNetwork> Create(scoped_refptr<dbus::Bus> bus,
                                            uint32_t vsock_cid);

  ~ArcNetwork() override;

  const patchpanel::Client::ArcVMAllocation& Allocation() const {
    return allocation_;
  }

 private:
  ArcNetwork(std::unique_ptr<patchpanel::Client> client,
             patchpanel::Client::ArcVMAllocation allocation,
             uint32_t vsock_cid);

  patchpanel::Client::ArcVMAllocation allocation_;
  uint32_t vsock_cid_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_NETWORK_ARC_NETWORK_H_

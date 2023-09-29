// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_NETWORK_PLUGIN_VM_NETWORK_H_
#define VM_TOOLS_CONCIERGE_NETWORK_PLUGIN_VM_NETWORK_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "chromeos/patchpanel/dbus/client.h"

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/network/scoped_network.h"

namespace dbus {
class Bus;
}

namespace vm_tools::concierge {

class PluginVmNetwork : public ScopedNetwork {
 public:
  static std::unique_ptr<PluginVmNetwork> Create(scoped_refptr<dbus::Bus> bus,
                                                 const VmId& id,
                                                 int subnet_index);

  ~PluginVmNetwork() override;

  const patchpanel::Client::ParallelsAllocation& Allocation() const {
    return allocation_;
  }

 private:
  PluginVmNetwork(std::unique_ptr<patchpanel::Client> client,
                  patchpanel::Client::ParallelsAllocation allocation,
                  uint64_t id);

  patchpanel::Client::ParallelsAllocation allocation_;
  uint64_t id_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_NETWORK_PLUGIN_VM_NETWORK_H_

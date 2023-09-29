// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_NETWORK_SCOPED_NETWORK_H_
#define VM_TOOLS_CONCIERGE_NETWORK_SCOPED_NETWORK_H_

#include <memory>

namespace patchpanel {
class Client;
}

namespace vm_tools::concierge {

// A handle to network resources allocated by patchpanel for concierge's VMs.
//
// This class performs two functions:
//  - Enforces scoping behaviour for network resources, so that network is
//    allocated precisely for the lifetime of the VM.
//  - Provides a hierarchy over network allocations (which patchpanel doesn't)
//    to match the hierarchy of vm implementations in concierge.
class ScopedNetwork {
 public:
  explicit ScopedNetwork(std::unique_ptr<patchpanel::Client> client);

  // Subclasses must implement logic to clean up the network allocation.
  virtual ~ScopedNetwork() = 0;

 protected:
  patchpanel::Client& client() { return *client_; }

 private:
  std::unique_ptr<patchpanel::Client> client_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_NETWORK_SCOPED_NETWORK_H_

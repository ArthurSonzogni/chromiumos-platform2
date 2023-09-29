// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/guest_os_network.h"

#include <utility>

#include "chromeos/patchpanel/dbus/client.h"

#include "vm_tools/concierge/network/scoped_network.h"

namespace vm_tools::concierge {

GuestOsNetwork::GuestOsNetwork(std::unique_ptr<patchpanel::Client> client,
                               uint32_t vsock_cid)
    : ScopedNetwork(std::move(client)), vsock_cid_(vsock_cid) {}

}  // namespace vm_tools::concierge

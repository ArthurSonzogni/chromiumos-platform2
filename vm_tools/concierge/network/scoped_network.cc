// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/network/scoped_network.h"

#include <utility>

#include "chromeos/patchpanel/dbus/client.h"

namespace vm_tools::concierge {

ScopedNetwork::ScopedNetwork(std::unique_ptr<patchpanel::Client> client)
    : client_(std::move(client)) {}

// Although the destructor is PV, we still need an implementation for
// child-classes to override.
ScopedNetwork::~ScopedNetwork() = default;

}  // namespace vm_tools::concierge

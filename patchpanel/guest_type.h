// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_GUEST_TYPE_H_
#define PATCHPANEL_GUEST_TYPE_H_

#include <ostream>

namespace patchpanel {

// Enum reprensenting the different types of downstream guests managed by
// patchpanel. The guest types corresponding to patchpanel Devices
// created by patchpanel directly are: ARC0, ARC_NET, VM_TERMINA, VM_PLUGIN.
// LXD_CONTAINER corresponds to user containers created inside a Termina VM.
// MINIJAIL_NETNS corresponds to a network namespace attached to the datapath
// with patchpanel ConnectNamespace API.
enum class GuestType {
  // ARC++ or ARCVM management interface.
  kArc0,
  // ARC++ or ARCVM virtual networks connected to shill Devices.
  kArcNet,
  /// Crostini VM root namespace.
  kTerminaVM,
  // Crostini plugin VMs.
  kPluginVM,
  // Crostini VM user containers.
  kLXDContainer,
  // Other network namespaces hosting minijailed host processes.
  kNetns,
};

std::ostream& operator<<(std::ostream& stream, const GuestType guest_type);

}  // namespace patchpanel

#endif  // PATCHPANEL_GUEST_TYPE_H_

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/guest_type.h"

namespace patchpanel {

std::ostream& operator<<(std::ostream& stream, const GuestType guest_type) {
  switch (guest_type) {
    case GuestType::kArc0:
      return stream << "ARC0";
    case GuestType::kArcNet:
      return stream << "ARC_NET";
    case GuestType::kVmTermina:
      return stream << "VM_TERMINA";
    case GuestType::kVmPlugin:
      return stream << "VM_PLUGIN";
    case GuestType::kLxdContainer:
      return stream << "LXD_CONTAINER";
    case GuestType::kNetns:
      return stream << "MINIJAIL_NETNS";
    default:
      return stream << "UNKNOWN";
  }
}

}  // namespace patchpanel

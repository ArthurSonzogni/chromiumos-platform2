// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SIBLING_VMS_H_
#define VM_TOOLS_CONCIERGE_SIBLING_VMS_H_

#include <stdint.h>

#include <vector>

namespace vm_tools {
namespace concierge {

// Parses all PCI devices, looks for any VVU devices and returns their
// corresponding VVU socket indices.
std::vector<int32_t> GetVvuDevicesSocketIndices();

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SIBLING_VMS_H_

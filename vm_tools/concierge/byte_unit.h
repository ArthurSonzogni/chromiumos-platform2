// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BYTE_UNIT_H_
#define VM_TOOLS_CONCIERGE_BYTE_UNIT_H_

#include <cstdint>

namespace vm_tools::concierge {
constexpr int64_t KiB(int64_t kilobytes) {
  return kilobytes * 1024;
}
constexpr int64_t MiB(int64_t megabytes) {
  return megabytes * KiB(1024);
}
constexpr int64_t GiB(int64_t gigabytes) {
  return gigabytes * MiB(1024);
}
}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_BYTE_UNIT_H_

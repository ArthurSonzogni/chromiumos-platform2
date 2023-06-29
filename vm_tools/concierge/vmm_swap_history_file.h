// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_HISTORY_FILE_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_HISTORY_FILE_H_

#include <base/files/file.h>

namespace vm_tools::concierge {

// Write a `Entry` into a history file.
//
// `Container` protobuf message must have `repeated Entry entries` field.
template <typename Container, typename Entry>
bool VmmSwapWriteEntry(const base::File& file, Entry entry) {
  // Consecutively serialized bytes from multiple Containers can be
  // deserialized as single merged Container.
  Container container;
  Entry* new_entry = container.add_entries();
  new_entry->Swap(&entry);
  return container.SerializeToFileDescriptor(file.GetPlatformFile());
}

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_HISTORY_FILE_H_

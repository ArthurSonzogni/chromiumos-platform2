// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MEMINFO_READER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MEMINFO_READER_H_

#include <cstdint>
#include <optional>

namespace diagnostics {

// Stores the system memory info from |/proc/meminfo|. For more details, see
// https://www.kernel.org/doc/Documentation/filesystems/proc.txt.
struct MemoryInfo {
  // Total usable memory, in KiB.
  uint64_t total_memory_kib;
  // Free memory, in KiB.
  uint64_t free_memory_kib;
  // Available memory for starting new applications without swapping, in KiB.
  uint64_t available_memory_kib;
  // Relatively temporary storage for raw disk blocks, in KiB.
  uint64_t buffers_kib;
  // In-memory cache for files read from the disk, in KiB.
  uint64_t page_cache_kib;
  // Shared memory, used in tmpfs, in KiB.
  uint64_t shared_memory_kib;
  // More recently used memory, in KiB.
  uint64_t active_memory_kib;
  // Less recently used memory, in KiB.
  uint64_t inactive_memory_kib;
  // Total swap memory, in KiB.
  uint64_t total_swap_memory_kib;
  // Free swap memory, in KiB.
  uint64_t free_swap_memory_kib;
  // The swapped back memory in KiB, but is still in the swap.
  uint64_t cached_swap_memory_kib;
  // Kernal-used memory, in KiB.
  uint64_t total_slab_memory_kib;
  // Reclaimable slab memory, in KiB.
  uint64_t reclaimable_slab_memory_kib;
  // Unreclaimable slab memory, in KiB.
  uint64_t unreclaimable_slab_memory_kib;
};

class MeminfoReader {
 public:
  MeminfoReader();
  MeminfoReader(const MeminfoReader&) = delete;
  MeminfoReader& operator=(const MeminfoReader&) = delete;
  virtual ~MeminfoReader();

  // Gets the parsing result of `/proc/meminfo`. Return std::nullopt if there is
  // a parse error.
  virtual std::optional<MemoryInfo> GetInfo() const;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MEMINFO_READER_H_

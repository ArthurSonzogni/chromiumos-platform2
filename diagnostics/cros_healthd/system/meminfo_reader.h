// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MEMINFO_READER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MEMINFO_READER_H_

#include <cstdint>
#include <optional>

namespace diagnostics {

// Stores the system memory info from |/proc/meminfo|.
struct MemoryInfo {
  uint64_t total_memory_kib;
  uint64_t free_memory_kib;
  uint64_t available_memory_kib;
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

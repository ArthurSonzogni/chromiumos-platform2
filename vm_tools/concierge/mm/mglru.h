// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_MGLRU_H_
#define VM_TOOLS_CONCIERGE_MM_MGLRU_H_

#include <optional>
#include <string>

#include <vm_memory_management/vm_memory_management.pb.h>

using vm_tools::vm_memory_management::MglruStats;

namespace vm_tools::concierge::mm::mglru {

// Parses MglruStats from the contents of the MGLRU sysfs admin file
// Usually: /sys/kernel/mm/lru_gen/admin.
// The admin file is in page units, so page_size is used to convert to KiB.
std::optional<MglruStats> ParseStatsFromString(
    const std::string_view stats_string, const size_t page_size);

// Formats the given stats into a human readable string
// page_size is used to convert from KiB (input) to page size units in the
// result string.
std::string StatsToString(const MglruStats& stats, const size_t page_size);

}  // namespace vm_tools::concierge::mm::mglru

#endif  // VM_TOOLS_CONCIERGE_MM_MGLRU_H_

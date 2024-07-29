// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/meminfo_reader.h"

#include <stdint.h>

#include <map>
#include <optional>
#include <string>

#include <base/containers/fixed_flat_set.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>

#include "diagnostics/base/file_utils.h"

namespace diagnostics {

namespace {

constexpr char kRelativeMeminfoPath[] = "proc/meminfo";
constexpr char kMemTotalName[] = "MemTotal";
constexpr char kMemFreeName[] = "MemFree";
constexpr char kMemAvailableName[] = "MemAvailable";
constexpr char kBuffersName[] = "Buffers";
constexpr char kCachedName[] = "Cached";
constexpr char kShmemName[] = "Shmem";
constexpr char kActiveName[] = "Active";
constexpr char kInactiveName[] = "Inactive";
constexpr char kSwapTotalName[] = "SwapTotal";
constexpr char kSwapFreeName[] = "SwapFree";
constexpr char kSwapCachedName[] = "SwapCached";
constexpr char kSlabName[] = "Slab";
constexpr char kSReclaimableName[] = "SReclaimable";
constexpr char kSUnreclaimName[] = "SUnreclaim";

bool ParseRow(std::string raw_value, uint64_t* out_value) {
  // Parse each line in /proc/meminfo.
  // Format of `raw_value`: "${PAD_SPACES}${MEM_AMOUNT} kB".
  base::StringTokenizer t(raw_value, " ");
  return t.GetNext() && base::StringToUint64(t.token(), out_value) &&
         t.GetNext() && t.token() == "kB";
}

std::optional<MemoryInfo> Parse(const std::string& raw_data) {
  // Format of `raw_data`: "${MEM_NAME}:${PAD_SPACES}${MEM_AMOUNT} kB".
  base::StringPairs pairs;
  if (!base::SplitStringIntoKeyValuePairs(raw_data, ':', '\n', &pairs)) {
    LOG(ERROR) << "Incorrectly formatted /proc/meminfo";
    return std::nullopt;
  }

  auto targetMemoryFields = base::MakeFixedFlatSet<std::string_view>(
      {kMemTotalName, kMemFreeName, kMemAvailableName, kBuffersName,
       kCachedName, kShmemName, kActiveName, kInactiveName, kSwapTotalName,
       kSwapFreeName, kSwapCachedName, kSlabName, kSReclaimableName,
       kSUnreclaimName});
  // Parse the meminfo contents for items in `targetMemoryFields`. Note
  // that these values are actually reported in KiB from /proc/meminfo, despite
  // claiming to be in kB.
  std::map<std::string_view, uint64_t> memory_map_kib;
  uint64_t out_memory_kib;
  for (const auto& [field_name, value] : pairs) {
    if (targetMemoryFields.contains(field_name)) {
      if (!ParseRow(value, &out_memory_kib)) {
        LOG(ERROR) << "Incorrectly formatted: " << field_name;
        return std::nullopt;
      }
      memory_map_kib[field_name] = out_memory_kib;
    }
  }

  for (const auto& memory_name : targetMemoryFields) {
    if (!memory_map_kib.contains(memory_name)) {
      LOG(ERROR) << memory_name << " not found in /proc/meminfo";
      return std::nullopt;
    }
  }

  return MemoryInfo{
      .total_memory_kib = memory_map_kib[kMemTotalName],
      .free_memory_kib = memory_map_kib[kMemFreeName],
      .available_memory_kib = memory_map_kib[kMemAvailableName],
      .buffers_kib = memory_map_kib[kBuffersName],
      .page_cache_kib = memory_map_kib[kCachedName],
      .shared_memory_kib = memory_map_kib[kShmemName],
      .active_memory_kib = memory_map_kib[kActiveName],
      .inactive_memory_kib = memory_map_kib[kInactiveName],
      .total_swap_memory_kib = memory_map_kib[kSwapTotalName],
      .free_swap_memory_kib = memory_map_kib[kSwapFreeName],
      .cached_swap_memory_kib = memory_map_kib[kSwapCachedName],
      .total_slab_memory_kib = memory_map_kib[kSlabName],
      .reclaimable_slab_memory_kib = memory_map_kib[kSReclaimableName],
      .unreclaimable_slab_memory_kib = memory_map_kib[kSUnreclaimName],
  };
}

}  // namespace

MeminfoReader::MeminfoReader() = default;
MeminfoReader::~MeminfoReader() = default;

std::optional<MemoryInfo> MeminfoReader::GetInfo() const {
  std::string file_contents;
  if (!ReadAndTrimString(GetRootDir().Append(kRelativeMeminfoPath),
                         &file_contents)) {
    LOG(ERROR) << "Unable to read /proc/meminfo";
    return std::nullopt;
  }
  return Parse(file_contents);
}

}  // namespace diagnostics

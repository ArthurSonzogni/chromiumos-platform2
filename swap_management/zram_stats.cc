// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/utils.h"
#include "swap_management/zram_stats.h"

#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <base/files/file_path.h>

namespace swap_management {

absl::Status ParseZramMmStat(const std::string& input,
                             ZramMmStat* zram_mm_stat) {
  std::vector<std::string> zram_mm_stat_list = base::SplitString(
      input, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Return false if the list size is less than number of items in ZramMmStat
  // From first version of Zram mm_stat in v4.4, there are seven fields inside.
  if (zram_mm_stat_list.size() < 7)
    return absl::InvalidArgumentError("Malformed zram mm_stat input");

  // In zram_drv.h we define max_used_pages as atomic_long_t which could
  // be negative, but negative value does not make sense for the
  // variable. return false if negative max_used_pages.
  int64_t tmp_mem_used_max = 0;
  if (!base::StringToInt64(zram_mm_stat_list[4], &tmp_mem_used_max) ||
      tmp_mem_used_max < 0)
    return absl::InvalidArgumentError("Bad value for zram max_used_pages");
  zram_mm_stat->mem_used_max = static_cast<uint64_t>(tmp_mem_used_max);

  bool status =
      base::StringToUint64(zram_mm_stat_list[0],
                           &zram_mm_stat->orig_data_size) &&
      base::StringToUint64(zram_mm_stat_list[1],
                           &zram_mm_stat->compr_data_size) &&
      base::StringToUint64(zram_mm_stat_list[2],
                           &zram_mm_stat->mem_used_total) &&
      base::StringToUint(zram_mm_stat_list[3], &zram_mm_stat->mem_limit) &&
      base::StringToUint64(zram_mm_stat_list[5], &zram_mm_stat->same_pages) &&
      base::StringToUint(zram_mm_stat_list[6], &zram_mm_stat->pages_compacted);

  constexpr static size_t kHugeIdx = 7;
  constexpr static size_t kHugeSinceIdx = 8;

  uint64_t value = 0;
  if (zram_mm_stat_list.size() > kHugeIdx) {
    status &= base::StringToUint64(zram_mm_stat_list[kHugeIdx], &value);
    if (status)
      zram_mm_stat->huge_pages = value;
  }
  if (zram_mm_stat_list.size() > kHugeSinceIdx) {
    status &= base::StringToUint64(zram_mm_stat_list[kHugeSinceIdx], &value);
    if (status)
      zram_mm_stat->huge_pages_since = value;
  }

  return status ? absl::OkStatus()
                : absl::InvalidArgumentError("Failed to parse zram mm_stat");
}

absl::Status ParseZramBdStat(const std::string& input,
                             ZramBdStat* zram_bd_stat) {
  std::vector<std::string> zram_bd_stat_list = base::SplitString(
      input, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Return false if the list size is less than number of items in ZramBdStat
  if (zram_bd_stat_list.size() < 3)
    return absl::InvalidArgumentError("Malformed zram bd_stat input");

  bool status =
      base::StringToUint64(zram_bd_stat_list[0], &zram_bd_stat->bd_count) &&
      base::StringToUint64(zram_bd_stat_list[1], &zram_bd_stat->bd_reads) &&
      base::StringToUint64(zram_bd_stat_list[2], &zram_bd_stat->bd_writes);

  return status ? absl::OkStatus()
                : absl::InvalidArgumentError("Failed to parse zram bd_stat");
}

absl::StatusOr<ZramBdStat> GetZramBdStat() {
  std::string buf;
  absl::Status status = Utils::Get()->ReadFileToString(
      base::FilePath(kZramSysfsDir).Append("bd_stat"), &buf);
  if (!status.ok())
    return status;

  ZramBdStat zram_bd_stat;
  status = ParseZramBdStat(buf, &zram_bd_stat);
  if (!status.ok())
    return status;

  return std::move(zram_bd_stat);
}

absl::StatusOr<ZramMmStat> GetZramMmStat() {
  std::string buf;
  absl::Status status = Utils::Get()->ReadFileToString(
      base::FilePath(kZramSysfsDir).Append("mm_stat"), &buf);
  if (!status.ok())
    return status;

  ZramMmStat zram_mm_stat;
  status = ParseZramMmStat(buf, &zram_mm_stat);
  if (!status.ok())
    return status;

  return std::move(zram_mm_stat);
}
}  // namespace swap_management

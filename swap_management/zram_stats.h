// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_ZRAM_STATS_H_
#define SWAP_MANAGEMENT_ZRAM_STATS_H_

#include <cstdint>
#include <optional>

#include <absl/status/status.h>
#include <absl/status/statusor.h>

namespace swap_management {

struct ZramMmStat {
  // Uncompressed size of data stored in this disk. This excludes
  // same-element-filled pages (same_pages) since no memory is allocated for
  // them. Unit: bytes
  uint64_t orig_data_size = 0;
  // Compressed size of data stored in this disk.
  uint64_t compr_data_size = 0;
  // The amount of memory allocated for this disk. This includes allocator
  // fragmentation and metadata overhead, allocated for this disk. So, allocator
  // space efficiency can be calculated using compr_data_size and this
  // statistic. Unit: bytes
  uint64_t mem_used_total = 0;
  // The maximum amount of memory ZRAM can use to store The compressed data.
  uint32_t mem_limit = 0;
  // The maximum amount of memory zram have consumed to store the data.
  uint64_t mem_used_max = 0;
  // The number of same element filled pages written to this disk. No memory is
  // allocated for such pages.
  uint64_t same_pages = 0;
  // The number of pages freed during compaction.
  uint32_t pages_compacted = 0;
  // The number of incompressible pages.
  // Start supporting from v4.19.
  std::optional<uint64_t> huge_pages;
  // The number of huge pages since zram set up.
  // Start supporting from v5.15.
  std::optional<uint64_t> huge_pages_since;
};

struct ZramBdStat {
  // Size of data written in backing device. Unit: 4K bytes
  uint64_t bd_count = 0;
  // The number of reads from backing device. Unit: 4K bytes
  uint64_t bd_reads = 0;
  // The number of writes to backing device. Unit: 4K bytes
  uint64_t bd_writes = 0;
};

struct ZramIoStat {
  // The number of failed reads.
  uint64_t failed_reads = 0;
  // The number of failed writes.
  uint64_t failed_writes = 0;
  // The number of non-page-size-aligned I/O requests
  uint64_t invalid_io = 0;
  // Depending on device usage scenario it may account a) the number of pages
  // freed because of swap slot free notifications or b) the number of pages
  // freed because of REQ_OP_DISCARD requests sent by bio. The former ones are
  // sent to a swap block device when a swap slot is freed, which implies that
  // this disk is being used as a swap disk. The latter ones are sent by
  // filesystem mounted with discard option, whenever some data blocks are
  // getting discarded.
  uint64_t notify_free = 0;
};

absl::StatusOr<ZramMmStat> GetZramMmStat();
absl::StatusOr<ZramBdStat> GetZramBdStat();

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_ZRAM_STATS_H_

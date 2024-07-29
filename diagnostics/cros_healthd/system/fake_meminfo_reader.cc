// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_meminfo_reader.h"

#include <cstdint>
#include <optional>

namespace diagnostics {

FakeMeminfoReader::FakeMeminfoReader() = default;

FakeMeminfoReader::~FakeMeminfoReader() = default;

std::optional<MemoryInfo> FakeMeminfoReader::GetInfo() const {
  if (is_error_) {
    return std::nullopt;
  }
  return MemoryInfo{
      .total_memory_kib = fake_total_memory_kib_,
      .free_memory_kib = fake_free_memory_kib_,
      .available_memory_kib = fake_available_memory_kib_,
      .buffers_kib = fake_buffers_kib_,
      .page_cache_kib = fake_page_cache_kib_,
      .shared_memory_kib = fake_shared_memory_kib_,
      .active_memory_kib = fake_active_memory_kib_,
      .inactive_memory_kib = fake_inactive_memory_kib_,
      .total_swap_memory_kib = fake_total_swap_memory_kib_,
      .free_swap_memory_kib = fake_free_swap_memory_kib_,
      .cached_swap_memory_kib = fake_cached_swap_memory_kib_,
      .total_slab_memory_kib = fake_total_slab_memory_kib_,
      .reclaimable_slab_memory_kib = fake_reclaimable_slab_memory_kib_,
      .unreclaimable_slab_memory_kib = fake_unreclaimable_slab_memory_kib_,
  };
}

void FakeMeminfoReader::SetError(bool value) {
  is_error_ = value;
}

void FakeMeminfoReader::SetTotalMemoryKib(uint64_t value) {
  fake_total_memory_kib_ = value;
}

void FakeMeminfoReader::SetFreeMemoryKib(uint64_t value) {
  fake_free_memory_kib_ = value;
}

void FakeMeminfoReader::SetAvailableMemoryKib(uint64_t value) {
  fake_available_memory_kib_ = value;
}

void FakeMeminfoReader::SetBuffersKib(uint64_t value) {
  fake_buffers_kib_ = value;
}

void FakeMeminfoReader::SetPageCacheKib(uint64_t value) {
  fake_page_cache_kib_ = value;
}

void FakeMeminfoReader::SetSharedMemoryKib(uint64_t value) {
  fake_shared_memory_kib_ = value;
}

void FakeMeminfoReader::SetActiveMemoryKib(uint64_t value) {
  fake_active_memory_kib_ = value;
}

void FakeMeminfoReader::SetInactiveMemoryKib(uint64_t value) {
  fake_inactive_memory_kib_ = value;
}

void FakeMeminfoReader::SetTotalSwapMemoryKib(uint64_t value) {
  fake_total_swap_memory_kib_ = value;
}

void FakeMeminfoReader::SetFreeSwapMemoryKib(uint64_t value) {
  fake_free_swap_memory_kib_ = value;
}

void FakeMeminfoReader::SetcachedSwapMemoryKib(uint64_t value) {
  fake_cached_swap_memory_kib_ = value;
}

void FakeMeminfoReader::SetTotalSlabMemoryKib(uint64_t value) {
  fake_total_slab_memory_kib_ = value;
}

void FakeMeminfoReader::SetReclaimableSlabMemoryKib(uint64_t value) {
  fake_reclaimable_slab_memory_kib_ = value;
}

void FakeMeminfoReader::SetUnreclaimableSlabMemoryKib(uint64_t value) {
  fake_unreclaimable_slab_memory_kib_ = value;
}

}  // namespace diagnostics

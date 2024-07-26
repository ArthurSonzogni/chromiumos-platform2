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
  };
}

void FakeMeminfoReader::SetError(bool value) {
  is_error_ = value;
}

void FakeMeminfoReader::SetTotalMemoryKib(uint32_t value) {
  fake_total_memory_kib_ = value;
}

void FakeMeminfoReader::SetFreeMemoryKib(uint32_t value) {
  fake_free_memory_kib_ = value;
}

void FakeMeminfoReader::SetAvailableMemoryKib(uint32_t value) {
  fake_available_memory_kib_ = value;
}

}  // namespace diagnostics

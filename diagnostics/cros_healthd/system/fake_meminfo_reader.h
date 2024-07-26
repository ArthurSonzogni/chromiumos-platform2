// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_MEMINFO_READER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_MEMINFO_READER_H_

#include <optional>

#include "diagnostics/cros_healthd/system/meminfo_reader.h"

namespace diagnostics {

class FakeMeminfoReader : public MeminfoReader {
 public:
  FakeMeminfoReader();
  FakeMeminfoReader(const FakeMeminfoReader&) = delete;
  FakeMeminfoReader& operator=(const FakeMeminfoReader&) = delete;
  ~FakeMeminfoReader() override;

  std::optional<MemoryInfo> GetInfo() const override;

  void SetError(bool value);
  void SetTotalMemoryKib(uint32_t value);
  void SetFreeMemoryKib(uint32_t value);
  void SetAvailableMemoryKib(uint32_t value);

 private:
  bool is_error_ = false;
  uint32_t fake_total_memory_kib_ = 0;
  uint32_t fake_free_memory_kib_ = 0;
  uint32_t fake_available_memory_kib_ = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_MEMINFO_READER_H_

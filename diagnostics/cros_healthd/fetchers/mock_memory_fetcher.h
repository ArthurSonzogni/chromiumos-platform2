// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MOCK_MEMORY_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MOCK_MEMORY_FETCHER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

namespace diagnostics {

class MockMemoryFetcher : public MemoryFetcher {
 public:
  MockMemoryFetcher() = default;
  ~MockMemoryFetcher() override = default;

  MOCK_METHOD(void,
              FetchMemoryInfo,
              (FetchMemoryInfoCallback callback),
              (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MOCK_MEMORY_FETCHER_H_

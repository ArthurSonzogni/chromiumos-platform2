// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_MOCK_STATIC_ANALYZER_H_
#define VTPM_BACKENDS_MOCK_STATIC_ANALYZER_H_

#include "vtpm/backends/static_analyzer.h"

#include <gmock/gmock.h>
#include <trunks/tpm_generated.h>

namespace vtpm {

class MockStaticAnalyzer : public StaticAnalyzer {
 public:
  ~MockStaticAnalyzer() override = default;
  MOCK_METHOD(int, GetCommandHandleCount, (trunks::TPM_CC cc), (override));
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_MOCK_STATIC_ANALYZER_H_

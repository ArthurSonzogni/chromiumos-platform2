// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_static_analyzer.h"

#include <base/logging.h>
#include <trunks/tpm_generated.h>

namespace vtpm {

int RealStaticAnalyzer::GetCommandHandleCount(trunks::TPM_CC cc) {
  return trunks::GetNumberOfRequestHandles(cc);
}

}  // namespace vtpm

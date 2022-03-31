// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_STATIC_ANALYZER_H_
#define VTPM_BACKENDS_STATIC_ANALYZER_H_

#include <trunks/tpm_generated.h>

namespace vtpm {

// This class defines a family of methods that judge the attributes of things,
// including but not lmited to a TPM command/response, w/ the knowldge on how
// TPM works.
class StaticAnalyzer {
 public:
  virtual ~StaticAnalyzer() = default;
  // Returns the number of handles that are required in a good `cc` command.
  virtual int GetCommandHandleCount(trunks::TPM_CC cc) = 0;
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_STATIC_ANALYZER_H_

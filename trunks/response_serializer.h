// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_RESPONSE_SERIALIZER_H_
#define TRUNKS_RESPONSE_SERIALIZER_H_

#include <string>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// A class that serialize TPM responses.
class TRUNKS_EXPORT ResponseSerializer {
 public:
  virtual ~ResponseSerializer() = default;

  // Serializes `rc` into a TPM command header.
  virtual void SerializeHeaderOnlyResponse(TPM_RC rc,
                                           std::string* response) = 0;
};
}  // namespace trunks

#endif  // TRUNKS_RESPONSE_SERIALIZER_H_

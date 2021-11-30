// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM2_ERROR_H_
#define LIBHWSEC_ERROR_TPM2_ERROR_H_

#include <memory>
#include <string>
#include <utility>

#include <trunks/error_codes.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/hwsec_export.h"

namespace hwsec {

// The error handler object for TPM2.
class HWSEC_EXPORT TPM2Error : public TPMErrorBase {
 public:
  struct MakeStatusTrait {
    auto operator()(trunks::TPM_RC error_code) {
      if (error_code != trunks::TPM_RC_SUCCESS) {
        return NewStatus<TPM2Error>(error_code);
      }
      return OkStatus<TPM2Error>();
    }
  };

  explicit TPM2Error(trunks::TPM_RC error_code);
  ~TPM2Error() override = default;
  TPMRetryAction ToTPMRetryAction() const override;
  trunks::TPM_RC ErrorCode() const { return error_code_; }

 private:
  const trunks::TPM_RC error_code_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM2_ERROR_H_

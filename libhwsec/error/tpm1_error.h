// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM1_ERROR_H_
#define LIBHWSEC_ERROR_TPM1_ERROR_H_

#include <memory>
#include <string>
#include <utility>

#include <trousers/tss.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/hwsec_export.h"

namespace hwsec {

// The error handler object for TPM1.
class HWSEC_EXPORT TPM1Error : public TPMErrorBase {
 public:
  struct MakeStatusTrait {
    auto operator()(TSS_RESULT error_code) {
      if (error_code != TSS_SUCCESS) {
        return NewStatus<TPM1Error>(error_code);
      }
      return OkStatus<TPM1Error>();
    }
  };

  explicit TPM1Error(TSS_RESULT error_code);
  ~TPM1Error() override = default;
  TPMRetryAction ToTPMRetryAction() const override;
  TSS_RESULT ErrorCode() const { return error_code_; }

 private:
  const TSS_RESULT error_code_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM1_ERROR_H_

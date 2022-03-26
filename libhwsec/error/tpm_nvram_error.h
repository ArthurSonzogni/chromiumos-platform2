// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_NVRAM_ERROR_H_
#define LIBHWSEC_ERROR_TPM_NVRAM_ERROR_H_

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/hwsec_export.h"

namespace hwsec {

// The error handler object for TPM NVRAM result.
class HWSEC_EXPORT TPMNvramError : public TPMErrorBase {
 public:
  using NvramResult = tpm_manager::NvramResult;

  struct MakeStatusTrait {
    auto operator()(NvramResult error_code) {
      using hwsec_foundation::status::NewStatus;
      using hwsec_foundation::status::OkStatus;

      if (error_code != NvramResult::NVRAM_RESULT_SUCCESS) {
        return NewStatus<TPMNvramError>(error_code);
      }
      return OkStatus<TPMNvramError>();
    }
  };

  explicit TPMNvramError(NvramResult error_code);
  ~TPMNvramError() override = default;
  TPMRetryAction ToTPMRetryAction() const override;
  NvramResult ErrorCode() const { return error_code_; }

 private:
  const NvramResult error_code_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_NVRAM_ERROR_H_

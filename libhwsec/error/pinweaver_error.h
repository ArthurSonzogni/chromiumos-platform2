// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_PINWEAVER_ERROR_H_
#define LIBHWSEC_ERROR_PINWEAVER_ERROR_H_

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/error/tpm_error.h"
#include "libhwsec/hwsec_export.h"

namespace hwsec {

// The error handler object for Pinweaver Manager result.
class HWSEC_EXPORT PinWeaverError : public TPMErrorBase {
 public:
  using PinWeaverErrorCode = PinWeaver::CredentialTreeResult::ErrorCode;

  struct MakeStatusTrait {
    auto operator()(PinWeaverErrorCode error_code) {
      using hwsec_foundation::status::NewStatus;
      using hwsec_foundation::status::OkStatus;

      if (error_code != PinWeaverErrorCode::kSuccess) {
        return NewStatus<PinWeaverError>(error_code);
      }
      return OkStatus<PinWeaverError>();
    }
  };

  explicit PinWeaverError(PinWeaverErrorCode error_code);
  ~PinWeaverError() override = default;
  TPMRetryAction ToTPMRetryAction() const override;
  PinWeaverErrorCode ErrorCode() const { return error_code_; }

  unified_tpm_error::UnifiedError UnifiedErrorCode() const override {
    unified_tpm_error::UnifiedError error_code =
        static_cast<unified_tpm_error::UnifiedError>(error_code_);
    error_code += unified_tpm_error::kUnifiedErrorPinWeaverBase;
    DCHECK_LT(error_code, unified_tpm_error::kUnifiedErrorPinWeaverMax);
    return error_code;
  }

 private:
  PinWeaverErrorCode error_code_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_PINWEAVER_ERROR_H_

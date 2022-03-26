// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_MANAGER_ERROR_H_
#define LIBHWSEC_ERROR_TPM_MANAGER_ERROR_H_

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/hwsec_export.h"

namespace hwsec {

// The error handler object for TPM Manager result.
class HWSEC_EXPORT TPMManagerError : public TPMErrorBase {
 public:
  using TpmManagerStatus = tpm_manager::TpmManagerStatus;

  struct MakeStatusTrait {
    auto operator()(TpmManagerStatus error_code) {
      using hwsec_foundation::status::NewStatus;
      using hwsec_foundation::status::OkStatus;

      if (error_code != TpmManagerStatus::STATUS_SUCCESS) {
        return NewStatus<TPMManagerError>(error_code);
      }
      return OkStatus<TPMManagerError>();
    }
  };

  explicit TPMManagerError(TpmManagerStatus error_code);
  ~TPMManagerError() override = default;
  TPMRetryAction ToTPMRetryAction() const override;
  TpmManagerStatus ErrorCode() const { return error_code_; }

 private:
  TpmManagerStatus error_code_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_MANAGER_ERROR_H_

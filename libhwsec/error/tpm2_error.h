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
namespace error {

// The error handler object for TPM2.
class HWSEC_EXPORT TPM2ErrorObj : public TPMErrorBaseObj {
 public:
  inline explicit TPM2ErrorObj(trunks::TPM_RC error_code)
      : error_code_(error_code) {}
  virtual ~TPM2ErrorObj() = default;
  std::string ToReadableString() const;
  hwsec_foundation::error::ErrorBase SelfCopy() const;
  TPMRetryAction ToTPMRetryAction() const;
  inline trunks::TPM_RC ErrorCode() { return error_code_; }

 protected:
  TPM2ErrorObj(TPM2ErrorObj&&) = default;

 private:
  const trunks::TPM_RC error_code_;
};
using TPM2Error = std::unique_ptr<TPM2ErrorObj>;

}  // namespace error
}  // namespace hwsec

namespace hwsec_foundation {
namespace error {

// Overload CreateError, so it would return nullptr when the |error_code|
// representing success.
template <typename ErrorType,
          typename T,
          typename std::enable_if<
              std::is_same<ErrorType, hwsec::error::TPM2Error>::value>::type* =
              nullptr,
          decltype(hwsec::error::TPM2ErrorObj(
              std::forward<T>(std::declval<T&&>())))* = nullptr>
hwsec::error::TPM2Error CreateError(T&& error_code) {
  if (error_code != trunks::TPM_RC_SUCCESS) {
    return std::make_unique<hwsec::error::TPM2ErrorObj>(
        std::forward<T>(error_code));
  }
  return nullptr;
}

}  // namespace error
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_ERROR_TPM2_ERROR_H_

// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_CRYPTOHOME_TPM_ERROR_H_
#define CRYPTOHOME_ERROR_CRYPTOHOME_TPM_ERROR_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <chromeos/dbus/service_constants.h>
#include <libhwsec/error/tpm_error.h>

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// This class is pretty much the same as the base CryptohomeError except that
// it's converted straight from TPMError. This class is needed because TPMError
// is not a derived class of CryptohomeError, but we need TPMError's information
// in the chain.
class CryptohomeTPMError : public CryptohomeError {
 public:
  struct MakeStatusTrait {
    // |Unactioned| represents an intermediate state, when we create an error
    // without fully specifying that error. That allows to require Wrap to be
    // called, or otherwise a type mismatch error will be raised.
    class Unactioned {
     public:
      Unactioned(const ErrorLocationPair& loc,
                 const std::set<CryptohomeError::Action>& actions);

      hwsec_foundation::status::StatusChain<CryptohomeTPMError> Wrap(
          hwsec_foundation::status::StatusChain<CryptohomeTPMError> status) &&;

     private:
      const ErrorLocationPair unified_loc_;
      const std::set<CryptohomeError::Action> actions_;
    };

    // Creates a stub which has to wrap another |hwsec::TPMErrorBase| or
    // |CryptohomeTPMError| to become a valid status chain.
    Unactioned operator()(const ErrorLocationPair& loc,
                          const std::set<CryptohomeError::Action>& actions);

    // Create an error directly.
    hwsec_foundation::status::StatusChain<CryptohomeTPMError> operator()(
        const ErrorLocationPair& loc,
        std::set<CryptohomeError::Action> actions,
        const hwsec::TPMRetryAction retry);

    // Create an error by converting |hwsec::TPMErrorBase|
    hwsec_foundation::status::StatusChain<CryptohomeTPMError> operator()(
        hwsec_foundation::status::StatusChain<hwsec::TPMErrorBase> error);
  };

  using BaseErrorType = CryptohomeError;

  // The copyable/movable aspect of this class depends on the base
  // hwsec_foundation::status::Error class. See that class for more info.

  // Note that different from other derived classes of |CryptohomeError|, this
  // class expects the ErrorLocation |loc| to be a unified error code. See
  // libhwsec's tpm_error.h for more information on the unified error code.
  CryptohomeTPMError(
      const ErrorLocationPair& loc,
      const std::set<CryptohomeError::Action>& actions,
      const hwsec::TPMRetryAction retry,
      std::optional<hwsec_foundation::status::StatusChain<hwsec::TPMErrorBase>>
          tpm_error,
      const std::optional<user_data_auth::CryptohomeErrorCode> ec);

  hwsec::TPMRetryAction ToTPMRetryAction() const { return retry_; }

 private:
  hwsec::TPMRetryAction retry_;

  base::Optional<hwsec_foundation::status::StatusChain<hwsec::TPMErrorBase>>
      tpm_error_;
};

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_CRYPTOHOME_TPM_ERROR_H_

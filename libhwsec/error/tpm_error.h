// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_ERROR_H_
#define LIBHWSEC_ERROR_TPM_ERROR_H_

#include <memory>
#include <string>
#include <utility>

#include <libhwsec/error/error.h>
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/hwsec_export.h"

/* The most important function of TPM error is representing a TPM retry action.
 *
 * MakeStatus<TPM1Error>/MakeStatus<TPM2Error> converts the raw error code from
 * the daemon to a Status object.
 *
 * For example:
 *   StatusChain<TPM1Error> status = MakeStatus<TPM1Error>(
 *       Tspi_TPM_CreateEndorsementKey(tpm_handle, local_key_handle, NULL));
 *
 * And it could also creating software based TPM error.
 *
 * For example:
 *   StatusChain<TPMError> status =
 *       MakeStatus<TPMError>("Failed to get trunks context",
 *                            TPMRetryAction::kNoRetry);
 *
 * Using Wrap() could wrap a TPM error into a new TPM error, and it
 * would transfer the retry action to the new TPM error (due to Wrap
 * overload).
 *
 * For example:
 *   if (StatusChain<TPMErrorBase> status = GetPublicKeyBlob(...))) {
 *     return MakeStatus<TPMError>("Failed to get TPM public key hash")
 *        .Wrap(std::move(status));
 *   }
 *
 * And it could also overwrite the original retry action.
 *
 * For example:
 *   if (StatusChain<TPM2Error> status = MakeStatus<TPM2Error>(...)) {
 *     return MakeStatus<TPMError>(
 *        "Error ...", TPMRetryAction::kNoRetry).Wrap(std::move(status));
 *   }
 *
 * It can also be used with status_macros helpers. For more info see
 * `platform2/libhwsec-foundation/error/status_macros.h`.
 *
 * RETURN_IF_ERROR(
 *     MakeStatus<TPM1Error>(
 *         Tspi_TPM_CreateEndorsementKey(tpm_handle, local_key_handle, NULL),
 *     AsStatusChain<TPMError>("Failed to create endorsement key")));
 */

namespace hwsec {

// A base class for TPM errors.
class HWSEC_EXPORT TPMErrorBase : public Error {
 public:
  using MakeStatusTrait = ForbidMakeStatus;

  explicit TPMErrorBase(std::string message);
  ~TPMErrorBase() override = default;

  // Returns what the action should do after this error happen.
  virtual TPMRetryAction ToTPMRetryAction() const = 0;
};

// A TPM error which contains error message and retry action. Doesn't contain
// an error code on its own.
class HWSEC_EXPORT TPMError : public TPMErrorBase {
 public:
  // Overload MakeStatus to prevent issuing un-actioned TPMErrors. Attempting to
  // create a StatusChain<TPMError> without an action will create a stub object
  // that caches the message and waits for the Wrap call with an appropriate
  // Status to complete the definition and construct a proper TPMError. That
  // intends to ensure that all TPMError object propagated contain an action
  // either explicitly specified or inherited from a specific tpm-type dependent
  // error object.
  struct MakeStatusTrait {
    class Unactioned {
     public:
      explicit Unactioned(std::string error_message)
          : error_message_(error_message) {}

      // Wrap will convert the stab into the appropriate Status type.
      auto Wrap(StatusChain<TPMErrorBase> status) && {
        return NewStatus<TPMError>(error_message_, status->ToTPMRetryAction())
            .Wrap(std::move(status));
      }

     private:
      const std::string error_message_;
    };

    // Returns a stub that doesn't convert to Status. The stub will wait for a
    // Wrap.
    auto operator()(std::string error_message) {
      return Unactioned(error_message);
    }

    // If we get action as an argument - create the Status directly.
    auto operator()(std::string error_message, TPMRetryAction action) {
      return NewStatus<TPMError>(error_message, action);
    }
  };

  TPMError(std::string error_message, TPMRetryAction action);
  ~TPMError() override = default;

  TPMRetryAction ToTPMRetryAction() const override { return retry_action_; }

 private:
  const TPMRetryAction retry_action_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_ERROR_H_

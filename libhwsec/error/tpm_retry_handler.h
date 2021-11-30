// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_RETRY_HANDLER_H_
#define LIBHWSEC_ERROR_TPM_RETRY_HANDLER_H_

#include <utility>

#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/hwsec_export.h"

namespace hwsec {

struct RetryInternalData {
  static constexpr int kMaxTryCount = 5;
  static constexpr base::TimeDelta kInitialRetry =
      base::TimeDelta::FromSecondsD(0.1);
  static constexpr double kRetryMultiplier = 2.0;

  int try_count = kMaxTryCount;
  base::TimeDelta current_wait = kInitialRetry;
};

void HWSEC_EXPORT RetryDelayHandler(RetryInternalData* data);

}  // namespace hwsec

#define HANDLE_TPM_COMM_ERROR(x)                                              \
  ({                                                                          \
    using return_type = decltype(x);                                          \
    static_assert(::hwsec_foundation::status::is_status_chain_v<return_type>, \
                  "The result type isn't a valid status type.");              \
    using wrapped_type = typename return_type::element_type;                  \
    static_assert(std::is_base_of_v<::hwsec::TPMErrorBase, wrapped_type>,     \
                  "The result type isn't a valid TPM error type.");           \
    hwsec::StatusChain<hwsec::TPMErrorBase> out_result;                       \
    ::hwsec::RetryInternalData retry_internal;                                \
    while (retry_internal.try_count > 0) {                                    \
      return_type tmp_result = (x);                                           \
      if (tmp_result.ok())                                                    \
        break;                                                                \
      if (tmp_result->ToTPMRetryAction() != TPMRetryAction::kCommunication) { \
        out_result = std::move(tmp_result);                                   \
        break;                                                                \
      }                                                                       \
      if (retry_internal.try_count <= 1) {                                    \
        out_result = hwsec::MakeStatus<TPMError>("Retry Failed",              \
                                                 TPMRetryAction::kLater)      \
                         .Wrap(std::move(tmp_result));                        \
        break;                                                                \
      }                                                                       \
      RetryDelayHandler(&retry_internal);                                     \
    }                                                                         \
    std::move(out_result);                                                    \
  })

#endif  // LIBHWSEC_ERROR_TPM_RETRY_HANDLER_H_

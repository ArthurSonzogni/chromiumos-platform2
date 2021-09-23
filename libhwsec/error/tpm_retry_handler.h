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
namespace error {

struct RetryInternalData {
  int retry_count = 0;
  base::TimeDelta current_wait;
};

bool HWSEC_EXPORT RetryCommHandler(TPMErrorBase* err, RetryInternalData* data);

}  // namespace error
}  // namespace hwsec

#define HANDLE_TPM_COMM_ERROR(x)                                             \
  ({                                                                         \
    using return_type = decltype(x);                                         \
    static_assert(                                                           \
        ::hwsec_foundation::error::is_error_type<return_type>::value,        \
        "The result type isn't a valid error type.");                        \
    static_assert(                                                           \
        std::is_base_of<::hwsec::error::TPMErrorBaseObj,                     \
                        typename ::hwsec_foundation::error::UnwarpErrorType< \
                            return_type>::type>::value,                      \
        "The result type isn't a valid TPM error type.");                    \
    ::hwsec::error::TPMErrorBase tpm_err_wrapper_result;                     \
    ::hwsec::error::RetryInternalData retry_internal;                        \
    do {                                                                     \
      tpm_err_wrapper_result = (x);                                          \
    } while (tpm_err_wrapper_result &&                                       \
             ::hwsec::error::RetryCommHandler(&tpm_err_wrapper_result,       \
                                              &retry_internal));             \
    std::move(tpm_err_wrapper_result);                                       \
  })

#endif  // LIBHWSEC_ERROR_TPM_RETRY_HANDLER_H_

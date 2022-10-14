// Copyright 2021 The ChromiumOS Authors
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
  static constexpr base::TimeDelta kInitialRetry = base::Seconds(0.1);
  static constexpr double kRetryMultiplier = 2.0;

  int try_count = kMaxTryCount;
  base::TimeDelta current_wait = kInitialRetry;
};

void HWSEC_EXPORT RetryDelayHandler(RetryInternalData* data);

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_RETRY_HANDLER_H_

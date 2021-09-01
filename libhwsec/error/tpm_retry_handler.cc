// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_handler.h"

namespace {
// Retry parameters for opening /dev/tpm0.
// How long do we wait after the first try?
constexpr base::TimeDelta kInitialRetry = base::TimeDelta::FromSecondsD(0.1);
// When we retry the next time, how much longer do we wait?
constexpr double kRetryMultiplier = 2.0;
// How many times to retry?
constexpr int kMaxRetry = 5;
}  // namespace

namespace hwsec {
namespace error {

bool RetryCommHandler(TPMError* err, RetryInternalData* data) {
  if ((*err)->ToTPMRetryAction() == TPMRetryAction::kCommunication) {
    if (data->retry_count + 1 >= kMaxRetry) {
      *err = hwsec_foundation::error::WrapError<TPMError>(
          std::move(*err), "Retry Failed", TPMRetryAction::kLater);
      return false;
    }
    if (!data->retry_count) {
      data->current_wait = kInitialRetry;
    }
    base::PlatformThread::Sleep(data->current_wait);
    data->current_wait = data->current_wait * kRetryMultiplier;
    data->retry_count++;
    return true;
  }
  return false;
}

}  // namespace error
}  // namespace hwsec

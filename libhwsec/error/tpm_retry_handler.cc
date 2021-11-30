// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_handler.h"

namespace hwsec {

void RetryDelayHandler(RetryInternalData* data) {
  base::PlatformThread::Sleep(data->current_wait);
  data->current_wait = data->current_wait * RetryInternalData::kRetryMultiplier;
  data->try_count--;
}

}  // namespace hwsec

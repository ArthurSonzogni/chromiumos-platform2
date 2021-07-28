// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/fuzzers/tpm_fuzzer_utils_impl.h"

#include <libhwsec/overalls/overalls_singleton.h>

namespace tpm_manager {

void TpmFuzzerUtilsImpl::SetupTpm(TpmManagerService* tpm_manager) {
  hwsec::overalls::OverallsSingleton::SetInstance(&mock_overalls_);
}

}  // namespace tpm_manager

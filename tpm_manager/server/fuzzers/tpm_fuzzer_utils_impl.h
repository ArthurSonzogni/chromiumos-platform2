// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_FUZZERS_TPM_FUZZER_UTILS_IMPL_H_
#define TPM_MANAGER_SERVER_FUZZERS_TPM_FUZZER_UTILS_IMPL_H_

#include <fuzzer/FuzzedDataProvider.h>
#include <gmock/gmock.h>
#include <libhwsec/overalls/mock_overalls.h>

#include "tpm_manager/server/fuzzers/tpm_fuzzer_utils.h"
#include "tpm_manager/server/tpm_manager_service.h"

namespace tpm_manager {

class TpmFuzzerUtilsImpl : public TpmFuzzerUtils {
 public:
  explicit TpmFuzzerUtilsImpl(FuzzedDataProvider*) {}
  void SetupTpm(TpmManagerService* tpm_manager) override;

 private:
  testing::NaggyMock<hwsec::overalls::MockOveralls> mock_overalls_;
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_FUZZERS_TPM_FUZZER_UTILS_IMPL_H_

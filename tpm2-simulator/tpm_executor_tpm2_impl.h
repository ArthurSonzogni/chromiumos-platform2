// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM2_SIMULATOR_TPM_EXECUTOR_TPM2_IMPL_H_
#define TPM2_SIMULATOR_TPM_EXECUTOR_TPM2_IMPL_H_

#include <string>

#include "tpm2-simulator/tpm_executor.h"

namespace tpm2_simulator {

class TpmExecutorTpm2Impl : public TpmExecutor {
 public:
  TpmExecutorTpm2Impl() = default;
  TpmExecutorTpm2Impl(const TpmExecutorTpm2Impl&) = delete;
  TpmExecutorTpm2Impl& operator=(const TpmExecutorTpm2Impl&) = delete;
  virtual ~TpmExecutorTpm2Impl() = default;

  void InitializeVTPM();
  size_t GetCommandSize(const std::string& command);
  std::string RunCommand(const std::string& command);
};

}  // namespace tpm2_simulator

#endif  // TPM2_SIMULATOR_TPM_EXECUTOR_TPM2_IMPL_H_

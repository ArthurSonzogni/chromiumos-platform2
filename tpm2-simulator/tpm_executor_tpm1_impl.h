// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM2_SIMULATOR_TPM_EXECUTOR_TPM1_IMPL_H_
#define TPM2_SIMULATOR_TPM_EXECUTOR_TPM1_IMPL_H_

#include <string>

#include "tpm2-simulator/tpm_executor.h"

namespace tpm2_simulator {

class TpmExecutorTpm1Impl : public TpmExecutor {
 public:
  TpmExecutorTpm1Impl() = default;
  TpmExecutorTpm1Impl(const TpmExecutorTpm1Impl&) = delete;
  TpmExecutorTpm1Impl& operator=(const TpmExecutorTpm1Impl&) = delete;
  virtual ~TpmExecutorTpm1Impl() = default;

  void InitializeVTPM() override;
  size_t GetCommandSize(const std::string& command) override;
  std::string RunCommand(const std::string& command) override;
  bool IsTPM2() override { return false; }
};

}  // namespace tpm2_simulator

#endif  // TPM2_SIMULATOR_TPM_EXECUTOR_TPM1_IMPL_H_

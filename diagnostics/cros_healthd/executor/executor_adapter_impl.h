// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_IMPL_H_

#include <string>

#include "diagnostics/cros_healthd/executor/executor_adapter.h"
#include "diagnostics/mojom/private/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Production implementation of the ExecutorAdapter interface.
class ExecutorAdapterImpl final : public ExecutorAdapter {
 public:
  ExecutorAdapterImpl();
  ExecutorAdapterImpl(const ExecutorAdapterImpl&) = delete;
  ExecutorAdapterImpl& operator=(const ExecutorAdapterImpl&) = delete;
  ~ExecutorAdapterImpl() override;

  // ExecutorAdapter overrides:
  void Connect(mojo::PlatformChannelEndpoint endpoint) override;
  void GetFanSpeed(Executor::GetFanSpeedCallback callback) override;
  void GetInterfaces(Executor::GetInterfacesCallback callback) override;
  void GetLink(const std::string& interface_name,
               Executor::GetLinkCallback callback) override;
  void GetInfo(const std::string& interface_name,
               Executor::GetInfoCallback callback) override;
  void GetScanDump(const std::string& interface_name,
                   Executor::GetScanDumpCallback callback) override;
  void RunMemtester(Executor::RunMemtesterCallback callback) override;
  void KillMemtester() override;
  void GetProcessIOContents(
      const pid_t pid,
      Executor::GetProcessIOContentsCallback callback) override;
  void RunModetest(
      chromeos::cros_healthd_executor::mojom::ModetestOptionEnum option,
      Executor::RunModetestCallback callback) override;
  void ReadMsr(const uint32_t msr_reg,
               Executor::ReadMsrCallback callback) override;

 private:
  // Mojo endpoint to call the executor's methods.
  chromeos::cros_healthd_executor::mojom::ExecutorPtr executor_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_IMPL_H_

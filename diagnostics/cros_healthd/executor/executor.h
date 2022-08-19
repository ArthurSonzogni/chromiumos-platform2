// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/task/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {
bool IsValidWirelessInterfaceName(const std::string& interface_name);

namespace mojom = chromeos::cros_healthd::mojom;

// Production implementation of the mojom::Executor Mojo interface.
class Executor final : public mojom::Executor {
 public:
  Executor(const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
           mojo::PendingReceiver<mojom::Executor> receiver);
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  // mojom::Executor overrides:
  void GetFanSpeed(GetFanSpeedCallback callback) override;
  void GetInterfaces(GetInterfacesCallback callback) override;
  void GetLink(const std::string& interface_name,
               GetLinkCallback callback) override;
  void GetInfo(const std::string& interface_name,
               GetInfoCallback callback) override;
  void GetScanDump(const std::string& interface_name,
                   GetScanDumpCallback callback) override;
  void RunMemtester(uint32_t test_mem_kib,
                    RunMemtesterCallback callback) override;
  void KillMemtester() override;
  void GetProcessIOContents(const uint32_t pid,
                            GetProcessIOContentsCallback callback) override;
  void ReadMsr(const uint32_t msr_reg,
               uint32_t cpu_index,
               ReadMsrCallback callback) override;
  void GetUEFISecureBootContent(
      GetUEFISecureBootContentCallback callback) override;
  void GetLidAngle(GetLidAngleCallback callback) override;

 private:
  // Runs the given binary with the given arguments and sandboxing. If
  // specified, |user| will be used as both the user and group for sandboxing
  // the binary. If not specified, the default cros_healthd:cros_healthd user
  // and group will be used. Does not track the process it launches, so the
  // launched process cannot be cancelled once it is started. If cancelling is
  // required, RunTrackedBinary() should be used instead.
  void RunUntrackedBinary(
      const base::FilePath& seccomp_policy_path,
      const std::vector<std::string>& sandboxing_args,
      const std::optional<std::string>& user,
      const base::FilePath& binary_path,
      const std::vector<std::string>& binary_args,
      mojom::ExecutedProcessResult result,
      base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback);
  // Like RunUntrackedBinary() above, but tracks the process internally so that
  // it can be cancelled if necessary.
  void RunTrackedBinary(
      const base::FilePath& seccomp_policy_path,
      const std::vector<std::string>& sandboxing_args,
      const std::optional<std::string>& user,
      const base::FilePath& binary_path,
      const std::vector<std::string>& binary_args,
      mojom::ExecutedProcessResult result,
      base::OnceCallback<void(mojom::ExecutedProcessResultPtr)> callback);
  // Helper function for RunUntrackedBinary() and RunTrackedBinary().
  int RunBinaryInternal(const base::FilePath& seccomp_policy_path,
                        const std::vector<std::string>& sandboxing_args,
                        const std::optional<std::string>& user,
                        const base::FilePath& binary_path,
                        const std::vector<std::string>& binary_args,
                        mojom::ExecutedProcessResult* result,
                        ProcessWithOutput* process);

  // Task runner for all Mojo callbacks.
  const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner_;

  // Provides a Mojo endpoint that cros_healthd can call to access the
  // executor's Mojo methods.
  mojo::Receiver<mojom::Executor> receiver_;

  // Prevents multiple simultaneous writes to |processes_|.
  base::Lock lock_;

  // Tracks running processes owned by the executor. Used to kill processes if
  // requested.
  std::map<std::string, std::unique_ptr<ProcessWithOutput>> processes_;

  // Must be the last member of the class.
  base::WeakPtrFactory<Executor> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_H_

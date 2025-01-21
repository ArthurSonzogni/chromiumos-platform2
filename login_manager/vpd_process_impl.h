// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_VPD_PROCESS_IMPL_H_
#define LOGIN_MANAGER_VPD_PROCESS_IMPL_H_

#include <signal.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>

#include "login_manager/subprocess.h"
#include "login_manager/system_utils.h"
#include "login_manager/vpd_process.h"

namespace brillo {
class ProcessReaper;
}  // namespace brillo

namespace login_manager {

class VpdProcessImpl : public VpdProcess {
 public:
  VpdProcessImpl(SystemUtils* system_utils,
                 brillo::ProcessReaper& process_reaper);
  ~VpdProcessImpl() override;

  // Ask the managed job to exit. |reason| is a human-readable string that may
  // be logged to describe the reason for the request.
  void RequestJobExit(const std::string& reason);

  // The job must be destroyed within the timeout.
  void EnsureJobExit(base::TimeDelta timeout);

  // Implementation of VpdProcess.
  bool RunInBackground(const KeyValuePairs& updates,
                       CompletionCallback completion) override;

 private:
  // Called on child process termination.
  void HandleExit(CompletionCallback callback, const siginfo_t& status);

  SystemUtils* system_utils_;  // Owned by the caller.
  raw_ref<brillo::ProcessReaper> process_reaper_;

  // The subprocess tracked by this job.
  std::unique_ptr<Subprocess> subprocess_;

  base::WeakPtrFactory<VpdProcessImpl> weak_factory_{this};
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_VPD_PROCESS_IMPL_H_

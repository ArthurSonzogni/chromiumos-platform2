// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_TASK_ENVIRONMENT_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_TASK_ENVIRONMENT_H_

#include <base/test/task_environment.h>
#include <base/threading/thread_task_runner_handle.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

namespace diagnostics {

// Sets up test environment for mojo.
class MojoTaskEnvironment : public base::test::SingleThreadTaskEnvironment {
 public:
  template <class... ArgTypes>
  explicit MojoTaskEnvironment(ArgTypes... args)
      : base::test::SingleThreadTaskEnvironment(MainThreadType::IO, args...),
        ipc_support_(base::ThreadTaskRunnerHandle::Get(),
                     mojo::core::ScopedIPCSupport::ShutdownPolicy::
                         CLEAN /* blocking shutdown */) {}

 private:
  mojo::core::ScopedIPCSupport ipc_support_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_TASK_ENVIRONMENT_H_

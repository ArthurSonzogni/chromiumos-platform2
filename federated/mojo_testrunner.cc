// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/threading/thread_task_runner_handle.h>
#include <brillo/message_loops/base_message_loop.h>
#include <common-mk/testrunner.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

// This test runner creates the platform2::TestRunner and initializes mojo for
// tests that require it. Mojo is not initialized by default.
int main(int argc, char** argv) {
  auto runner = platform2::TestRunner(argc, argv);

  (new brillo::BaseMessageLoop())->SetAsCurrent();

  mojo::core::Init();
  const mojo::core::ScopedIPCSupport ipc_support(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  return runner.Run();
}

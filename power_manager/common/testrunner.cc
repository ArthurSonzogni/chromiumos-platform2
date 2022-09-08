// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  logging::InitLogging(logging::LoggingSettings());
  logging::SetMinLogLevel(logging::LOGGING_WARNING);
  base::AtExitManager at_exit_manager;
  TestTimeouts::Initialize();
  // TODO(crbug/1094927): Use SingleThreadkTaskEnvironment.
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO);
  ::testing::InitGoogleTest(&argc, argv);

  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      task_environment.GetMainThreadTaskRunner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  return RUN_ALL_TESTS();
}

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <base/threading/thread_task_runner_handle.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

namespace diagnostics {
namespace bindings {
namespace connectivity {
namespace test {
namespace {

class MojoConnectivityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ::mojo::core::Init();
    ipc_support_ = std::make_unique<::mojo::core::ScopedIPCSupport>(
        base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
        ::mojo::core::ScopedIPCSupport::ShutdownPolicy::
            CLEAN /* blocking shutdown */);
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
};

}  // namespace
}  // namespace test
}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

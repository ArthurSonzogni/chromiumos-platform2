// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/threading/thread_task_runner_handle.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/bindings/connectivity/context.h"
#include "diagnostics/bindings/connectivity/local_state.h"
#include "diagnostics/bindings/connectivity/remote_state.h"
#include "diagnostics/bindings/connectivity/test/test_client.mojom-connectivity.h"
#include "diagnostics/bindings/connectivity/test/test_server.mojom-connectivity.h"

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

    mojo::PendingReceiver<mojom::State> receiver;
    auto remote = receiver.InitWithNewPipeAndPassRemote();
    context_ = Context::Create(LocalState::Create(std::move(receiver)),
                               RemoteState::Create(std::move(remote)));
  }

  Context* context() { return context_.get(); }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  std::unique_ptr<Context> context_;
};

template <typename T>
int CountPossibleValues(T type) {
  int cnt = 0;
  while (type->HasNext()) {
    ++cnt;
    type->Generate();
  }
  return cnt;
}

TEST_F(MojoConnectivityTest, DataGenerator) {
  ASSERT_EQ(CountPossibleValues(
                server::mojom::TestSuccessTestProvider::Create(context())),
            1);
  ASSERT_EQ(CountPossibleValues(
                server::mojom::TestSuccessTestConsumer::Create(context())),
            1);
}

template <typename ConsumerType>
bool Check(ConsumerType* consumer) {
  base::RunLoop run_loop;
  bool res;
  consumer->Check(base::BindLambdaForTesting([&](bool res_inner) {
    res = res_inner;
    run_loop.Quit();
  }));
  run_loop.Run();
  return res;
}

#define INTERFACE_TEST_BASE(INTERFACE_NAME)                              \
  auto provider =                                                        \
      server::mojom::INTERFACE_NAME##TestProvider::Create(context());    \
  ASSERT_NE(provider, nullptr);                                          \
  auto consumer =                                                        \
      client::mojom::INTERFACE_NAME##TestConsumer::Create(context());    \
  ASSERT_NE(consumer, nullptr);                                          \
  auto pending_receiver = consumer->Generate();                          \
  provider->Bind(::mojo::PendingReceiver<server::mojom::INTERFACE_NAME>( \
      pending_receiver.PassPipe()));

#define SUCCESSFUL_TEST(INTERFACE_NAME)          \
  TEST_F(MojoConnectivityTest, INTERFACE_NAME) { \
    INTERFACE_TEST_BASE(INTERFACE_NAME);         \
    EXPECT_TRUE(Check(consumer.get()));          \
  }

#define FAILED_TEST(INTERFACE_NAME)              \
  TEST_F(MojoConnectivityTest, INTERFACE_NAME) { \
    INTERFACE_TEST_BASE(INTERFACE_NAME);         \
    EXPECT_FALSE(Check(consumer.get()));         \
  }

SUCCESSFUL_TEST(TestSuccess);

FAILED_TEST(TestMissFunction);

}  // namespace
}  // namespace test
}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

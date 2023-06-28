// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/led/led_lit_up_v2.h"

#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::InvokeWithoutArgs;

constexpr auto kArbitraryLedName = mojom::LedName::kBattery;
constexpr auto kArbitraryLedColor = mojom::LedColor::kRed;

// Returns the exception info.
std::tuple<uint32_t, std::string> RunRoutineAndWaitForException(
    std::unique_ptr<BaseRoutineControl>& routine) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  routine->SetOnExceptionCallback(future.GetCallback());
  routine->Start();
  return future.Take();
}

mojom::RoutineStatePtr GetRoutineState(
    std::unique_ptr<BaseRoutineControl>& routine) {
  base::test::TestFuture<mojom::RoutineStatePtr> future;
  routine->GetState(future.GetCallback());
  return future.Take();
}

void OnUnexpectedException(uint32_t error, const std::string& reason) {
  CHECK(false) << "An exception has occurred when it shouldn't have.";
}

class MockLedLitUpRoutineReplier : public mojom::LedLitUpRoutineReplier {
 public:
  // mojom::LedLitUpRoutineReplier overrides:
  MOCK_METHOD(void, GetColorMatched, (GetColorMatchedCallback), (override));

  void Disconnect() { receiver.reset(); }

  mojo::Receiver<mojom::LedLitUpRoutineReplier> receiver{this};
};

class LedLitUpRoutineV2Test : public testing::Test {
 protected:
  void CreateRoutine(bool without_replier = false) {
    auto arg = mojom::LedLitUpRoutineArgument::New();
    arg->name = kArbitraryLedName;
    arg->color = kArbitraryLedColor;
    if (!without_replier) {
      arg->replier = mock_replier.receiver.BindNewPipeAndPassRemote();
    }
    routine =
        std::make_unique<LedLitUpV2Routine>(&mock_context, std::move(arg));
  }

  mojom::RoutineStatePtr StartRoutineAndWaitForResult() {
    base::RunLoop run_loop;
    RoutineObserverForTesting observer{run_loop.QuitClosure()};
    routine->SetOnExceptionCallback(base::BindOnce(&OnUnexpectedException));
    routine->AddObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine->Start();
    run_loop.Run();
    return std::move(observer.state_);
  }

  void SetExecutorSetLedColorResponse(const std::optional<std::string>& err) {
    EXPECT_CALL(*mock_context.mock_executor(), SetLedColor)
        .WillOnce(base::test::RunOnceCallback<2>(err));
  }

  void SetExecutorResetLedColorResponse(const std::optional<std::string>& err) {
    EXPECT_CALL(*mock_context.mock_executor(), ResetLedColor)
        .WillOnce(base::test::RunOnceCallback<1>(err));
  }

  void SetReplierGetColorMatchedResponse(bool matched) {
    EXPECT_CALL(mock_replier, GetColorMatched)
        .WillOnce(base::test::RunOnceCallback<0>(matched));
  }

  base::test::SingleThreadTaskEnvironment task_environment;
  std::unique_ptr<BaseRoutineControl> routine;
  MockLedLitUpRoutineReplier mock_replier;
  MockContext mock_context;
};

TEST_F(LedLitUpRoutineV2Test, InitializedStateBeforeStart) {
  CreateRoutine();
  auto result = GetRoutineState(routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_initialized());
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenCreatedWithInvalidReplier) {
  CreateRoutine(/*without_replier*/ true);
  auto [error_unused, reason] = RunRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Invalid replier.");
}

TEST_F(LedLitUpRoutineV2Test, SetLedColorError) {
  CreateRoutine();
  SetExecutorSetLedColorResponse("Error");

  auto [error_unused, reason] = RunRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Failed to set LED color.");
}

TEST_F(LedLitUpRoutineV2Test,
       ErrorWhenReplierDisconnectedBeforeCallingReplier) {
  CreateRoutine();
  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);
  mock_replier.Disconnect();

  auto [error_unused, reason] = RunRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Replier disconnected.");
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenReplierDisconnectedAfterCallingReplier) {
  CreateRoutine();
  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  // Disconnect the replier when waiting for the response of |GetColorMatched|.
  EXPECT_CALL(mock_replier, GetColorMatched)
      .WillOnce(InvokeWithoutArgs(&mock_replier,
                                  &MockLedLitUpRoutineReplier::Disconnect));
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto [error_unused, reason] = RunRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Replier disconnected.");
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenResetLedColorFailed) {
  CreateRoutine();
  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(true);
  SetExecutorResetLedColorResponse("Error");

  auto [error_unused, reason] = RunRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Failed to reset LED color.");
}

TEST_F(LedLitUpRoutineV2Test, FailedWhenColorNotMatched) {
  CreateRoutine();
  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(false);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto result = StartRoutineAndWaitForResult();
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

TEST_F(LedLitUpRoutineV2Test, PassedWhenColorMatched) {
  CreateRoutine();
  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(true);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto result = StartRoutineAndWaitForResult();
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

TEST_F(LedLitUpRoutineV2Test,
       NoCrashWhenReplierDisconnectAfterRoutineFinished) {
  CreateRoutine();
  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(true);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto result = StartRoutineAndWaitForResult();
  EXPECT_TRUE(result->state_union->is_finished());

  mock_replier.Disconnect();
  // Wait until the disconnection takes effect.
  task_environment.RunUntilIdle();
  // Expect no crash.
}

}  // namespace
}  // namespace diagnostics

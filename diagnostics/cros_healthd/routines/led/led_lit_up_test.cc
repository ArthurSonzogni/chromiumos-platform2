// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/led/led_lit_up.h"

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

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::InvokeWithoutArgs;

constexpr auto kArbitraryLedName = mojom::LedName::kBattery;
constexpr auto kArbitraryLedColor = mojom::LedColor::kRed;

// Returns the exception info.
std::tuple<uint32_t, std::string> RunRoutineAndWaitForException(
    BaseRoutineControl& routine) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  routine.SetOnExceptionCallback(future.GetCallback());
  routine.Start();
  return future.Take();
}

mojom::RoutineStatePtr GetRoutineState(BaseRoutineControl& routine) {
  base::test::TestFuture<mojom::RoutineStatePtr> future;
  routine.GetState(future.GetCallback());
  return future.Take();
}

mojom::RoutineStatePtr StartRoutineAndWaitForResult(
    BaseRoutineControl& routine) {
  routine.SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer;
  routine.SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine.Start();
  observer.WaitUntilRoutineFinished();
  return std::move(observer.state_);
}

class MockLedLitUpRoutineReplier : public mojom::LedLitUpRoutineReplier {
 public:
  // mojom::LedLitUpRoutineReplier overrides:
  MOCK_METHOD(void, GetColorMatched, (GetColorMatchedCallback), (override));

  void Disconnect() { receiver.reset(); }

  mojo::Receiver<mojom::LedLitUpRoutineReplier> receiver{this};
};

struct CreateRoutineOption {
  // The target LED name.
  mojom::LedName led_name = kArbitraryLedName;
  // The target LED color.
  mojom::LedColor led_color = kArbitraryLedColor;
  // Whether to create routine argument without the replier.
  bool without_replier = false;
};

class LedLitUpRoutineV2Test : public BaseFileTest {
 protected:
  void SetUp() override { SetFile(paths::sysfs::kCrosEc, ""); }

  base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
  CreateRoutine(CreateRoutineOption option = {}) {
    auto arg = mojom::LedLitUpRoutineArgument::New();
    arg->name = option.led_name;
    arg->color = option.led_color;
    if (!option.without_replier) {
      arg->replier = mock_replier_.receiver.BindNewPipeAndPassRemote();
    }
    return LedLitUpRoutine::Create(&mock_context_, std::move(arg));
  }

  void SetExecutorSetLedColorResponse(const std::optional<std::string>& err) {
    EXPECT_CALL(*mock_context_.mock_executor(), SetLedColor)
        .WillOnce(base::test::RunOnceCallback<2>(err));
  }

  void SetExecutorResetLedColorResponse(const std::optional<std::string>& err) {
    EXPECT_CALL(*mock_context_.mock_executor(), ResetLedColor)
        .WillOnce(base::test::RunOnceCallback<1>(err));
  }

  void SetReplierGetColorMatchedResponse(bool matched) {
    EXPECT_CALL(mock_replier_, GetColorMatched)
        .WillOnce(base::test::RunOnceCallback<0>(matched));
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  MockLedLitUpRoutineReplier mock_replier_;
  MockContext mock_context_;
};

TEST_F(LedLitUpRoutineV2Test, UnsupportedForUnexpectedLedName) {
  auto routine_create =
      CreateRoutine({.led_name = mojom::LedName::kUnmappedEnumField});
  ASSERT_FALSE(routine_create.has_value());
  auto support_status = std::move(routine_create.error());

  ASSERT_TRUE(support_status->is_unsupported());
  EXPECT_EQ(support_status->get_unsupported(),
            mojom::Unsupported::New("Unexpected LED name", /*reason=*/nullptr));
}

TEST_F(LedLitUpRoutineV2Test, UnsupportedForUnexpectedLedColor) {
  auto routine_create =
      CreateRoutine({.led_color = mojom::LedColor::kUnmappedEnumField});
  ASSERT_FALSE(routine_create.has_value());
  auto support_status = std::move(routine_create.error());

  ASSERT_TRUE(support_status->is_unsupported());
  EXPECT_EQ(
      support_status->get_unsupported(),
      mojom::Unsupported::New("Unexpected LED color", /*reason=*/nullptr));
}

TEST_F(LedLitUpRoutineV2Test, InitializedStateBeforeStart) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  auto result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_initialized());
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenCreatedWithInvalidReplier) {
  auto routine_create = CreateRoutine({.without_replier = true});
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  auto [error_unused, reason] = RunRoutineAndWaitForException(*routine);
  EXPECT_EQ(reason, "Invalid replier.");
}

TEST_F(LedLitUpRoutineV2Test, SetLedColorError) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse("Error");

  auto [error_unused, reason] = RunRoutineAndWaitForException(*routine);
  EXPECT_EQ(reason, "Failed to set LED color.");
}

TEST_F(LedLitUpRoutineV2Test,
       ErrorWhenReplierDisconnectedBeforeCallingReplier) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);
  mock_replier_.Disconnect();

  auto [error_unused, reason] = RunRoutineAndWaitForException(*routine);
  EXPECT_EQ(reason, "Replier disconnected.");
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenReplierDisconnectedAfterCallingReplier) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  // Disconnect the replier when waiting for the response of |GetColorMatched|.
  EXPECT_CALL(mock_replier_, GetColorMatched)
      .WillOnce(InvokeWithoutArgs(&mock_replier_,
                                  &MockLedLitUpRoutineReplier::Disconnect));
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto [error_unused, reason] = RunRoutineAndWaitForException(*routine);
  EXPECT_EQ(reason, "Replier disconnected.");
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenResetLedColorFailed) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(true);
  SetExecutorResetLedColorResponse("Error");

  auto [error_unused, reason] = RunRoutineAndWaitForException(*routine);
  EXPECT_EQ(reason, "Failed to reset LED color.");
}

TEST_F(LedLitUpRoutineV2Test, FailedWhenColorNotMatched) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(false);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto result = StartRoutineAndWaitForResult(*routine);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

TEST_F(LedLitUpRoutineV2Test, PassedWhenColorMatched) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(true);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto result = StartRoutineAndWaitForResult(*routine);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

TEST_F(LedLitUpRoutineV2Test,
       NoCrashWhenReplierDisconnectAfterRoutineFinished) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetReplierGetColorMatchedResponse(true);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);

  auto result = StartRoutineAndWaitForResult(*routine);
  EXPECT_TRUE(result->state_union->is_finished());

  mock_replier_.Disconnect();
  // Wait until the disconnection takes effect.
  task_environment_.RunUntilIdle();
  // Expect no crash.
}

}  // namespace
}  // namespace diagnostics

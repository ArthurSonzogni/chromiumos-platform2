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
  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  routine.SetOnExceptionCallback(exception_future.GetCallback());
  routine.Start();
  return exception_future.Take();
}

mojom::RoutineStatePtr GetRoutineState(BaseRoutineControl& routine) {
  base::test::TestFuture<mojom::RoutineStatePtr> state_future;
  routine.GetState(state_future.GetCallback());
  return state_future.Take();
}

mojom::RoutineInquiryReplyPtr CreateInquiryReply(
    mojom::CheckLedLitUpStateReply::State state) {
  return mojom::RoutineInquiryReply::NewCheckLedLitUpState(
      mojom::CheckLedLitUpStateReply::New(state));
}

struct CreateRoutineOption {
  // The target LED name.
  mojom::LedName led_name = kArbitraryLedName;
  // The target LED color.
  mojom::LedColor led_color = kArbitraryLedColor;
};

class LedLitUpRoutineV2Test : public BaseFileTest {
 protected:
  void SetUp() override { SetFile(paths::sysfs::kCrosEc, ""); }

  base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
  CreateRoutine(CreateRoutineOption option = {}) {
    auto arg = mojom::LedLitUpRoutineArgument::New();
    arg->name = option.led_name;
    arg->color = option.led_color;
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

  base::test::SingleThreadTaskEnvironment task_environment_;
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

TEST_F(LedLitUpRoutineV2Test, SetLedColorError) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse("Error");

  auto [error_unused, reason] = RunRoutineAndWaitForException(*routine);
  EXPECT_EQ(reason, "Failed to set LED color.");
}

TEST_F(LedLitUpRoutineV2Test, ErrorWhenResetLedColorFailed) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetExecutorResetLedColorResponse("Error");

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  routine->SetOnExceptionCallback(exception_future.GetCallback());

  RoutineObserverForTesting observer;
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();
  observer.WaitUntilRoutineWaiting();
  routine->ReplyInquiry(
      CreateInquiryReply(mojom::CheckLedLitUpStateReply::State::kCorrectColor));

  auto [error_unused, reason] = exception_future.Take();
  EXPECT_EQ(reason, "Failed to reset LED color.");
}

TEST_F(LedLitUpRoutineV2Test, FailedWhenColorNotMatched) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());

  RoutineObserverForTesting observer;
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();
  observer.WaitUntilRoutineWaiting();
  routine->ReplyInquiry(
      CreateInquiryReply(mojom::CheckLedLitUpStateReply::State::kNotLitUp));
  observer.WaitUntilRoutineFinished();

  const auto& result = observer.state_;
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

TEST_F(LedLitUpRoutineV2Test, PassedWhenColorMatched) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  SetExecutorSetLedColorResponse(/*err*/ std::nullopt);
  SetExecutorResetLedColorResponse(/*err*/ std::nullopt);
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());

  RoutineObserverForTesting observer;
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();
  observer.WaitUntilRoutineWaiting();
  routine->ReplyInquiry(
      CreateInquiryReply(mojom::CheckLedLitUpStateReply::State::kCorrectColor));
  observer.WaitUntilRoutineFinished();

  const auto& result = observer.state_;
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

}  // namespace
}  // namespace diagnostics

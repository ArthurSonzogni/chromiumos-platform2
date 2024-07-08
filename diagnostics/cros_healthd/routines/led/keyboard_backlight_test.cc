// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/led/keyboard_backlight.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <power_manager/dbus-proxy-mocks.h>
#include <power_manager/proto_bindings/backlight.pb.h>

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

constexpr double kDefaultBrightnessPercent = 1.0;

using ::testing::_;

mojom::RoutineStatePtr GetRoutineState(BaseRoutineControl& routine) {
  base::test::TestFuture<mojom::RoutineStatePtr> state_future;
  routine.GetState(state_future.GetCallback());
  return state_future.Take();
}

mojom::RoutineInquiryReplyPtr CreateInquiryReply(
    mojom::CheckKeyboardBacklightStateReply::State state) {
  return mojom::RoutineInquiryReply::NewCheckKeyboardBacklightState(
      mojom::CheckKeyboardBacklightStateReply::New(state));
}

class KeyboardBacklightRoutineTest : public BaseFileTest {
 public:
  KeyboardBacklightRoutineTest() = default;
  KeyboardBacklightRoutineTest(const KeyboardBacklightRoutineTest&) = delete;
  KeyboardBacklightRoutineTest& operator=(const KeyboardBacklightRoutineTest&) =
      delete;

 protected:
  base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
  CreateRoutine() {
    return KeyboardBacklightRoutine::Create(
        &mock_context_, mojom::KeyboardBacklightRoutineArgument::New());
  }

  void SetUp() override {
    // Assume the keyboard backlight is available for testing.
    SetKeyboardBacklightConfig("true");
  }

  void SetKeyboardBacklightConfig(const std::optional<std::string>& value) {
    SetFakeCrosConfig(paths::cros_config::kKeyboardBacklight, value);
  }

  void ExpectInitialGetBrightness() {
    EXPECT_CALL(*mock_power_manager_proxy(),
                GetKeyboardBrightnessPercentAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(kDefaultBrightnessPercent));
  }

  void ExpectConfigRestoration() {
    EXPECT_CALL(*mock_power_manager_proxy(),
                SetKeyboardBrightnessAsync(_, _, _, _))
        .WillOnce(base::test::RunOnceCallback<1>());
    EXPECT_CALL(*mock_power_manager_proxy(),
                SetKeyboardAmbientLightSensorEnabledAsync(true, _, _, _))
        .WillOnce(base::test::RunOnceCallback<1>());
  }

  void ExpectSetBrightnessForTestingAndConfigRestoration() {
    // Expect SetKeyboardBrightnessAsync called one more time due to config
    // restoration.
    EXPECT_CALL(*mock_power_manager_proxy(),
                SetKeyboardBrightnessAsync(_, _, _, _))
        .Times(num_percent_to_test_ + 1)
        .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<1>());
    EXPECT_CALL(*mock_power_manager_proxy(),
                SetKeyboardAmbientLightSensorEnabledAsync(_, _, _, _))
        .WillOnce(base::test::RunOnceCallback<1>());
  }

  void FastForwardToRoutineWaiting() {
    task_environment_.FastForwardBy(
        KeyboardBacklightRoutine::kTimeToStayAtEachPercent *
        num_percent_to_test_);
  }

  org::chromium::PowerManagerProxyMock* mock_power_manager_proxy() {
    return mock_context_.mock_power_manager_proxy();
  }

  uint32_t num_percent_to_test() { return num_percent_to_test_; }

 private:
  const uint32_t num_percent_to_test_ =
      static_cast<uint32_t>(
          KeyboardBacklightRoutine::kMaxBrightnessPercentToTest -
          KeyboardBacklightRoutine::kMinBrightnessPercentToTest) /
          KeyboardBacklightRoutine::kBrightnessPercentToTestIncrement +
      1;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
};

TEST_F(KeyboardBacklightRoutineTest, UnsupportedWithMissingCrosConfig) {
  SetKeyboardBacklightConfig(std::nullopt);
  auto routine_create = CreateRoutine();
  ASSERT_FALSE(routine_create.has_value());
  auto support_status = std::move(routine_create.error());

  EXPECT_TRUE(support_status->is_unsupported());
}

TEST_F(KeyboardBacklightRoutineTest, UnsupportedWithFalseCrosConfig) {
  SetKeyboardBacklightConfig("false");
  auto routine_create = CreateRoutine();
  ASSERT_FALSE(routine_create.has_value());
  auto support_status = std::move(routine_create.error());

  EXPECT_TRUE(support_status->is_unsupported());
}

TEST_F(KeyboardBacklightRoutineTest, InitializedStateBeforeStart) {
  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  auto result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_initialized());
}

TEST_F(KeyboardBacklightRoutineTest, PassedWithClientRepliedOk) {
  ExpectInitialGetBrightness();
  ExpectSetBrightnessForTestingAndConfigRestoration();

  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer;
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();

  FastForwardToRoutineWaiting();
  observer.WaitUntilRoutineWaiting();
  routine->ReplyInquiry(
      CreateInquiryReply(mojom::CheckKeyboardBacklightStateReply::State::kOk));
  observer.WaitUntilRoutineFinished();

  const auto& result = observer.state_;
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

TEST_F(KeyboardBacklightRoutineTest, FailedWithClientRepliedAnyNotLitUp) {
  ExpectInitialGetBrightness();
  ExpectSetBrightnessForTestingAndConfigRestoration();

  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());

  RoutineObserverForTesting observer;
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();
  FastForwardToRoutineWaiting();
  observer.WaitUntilRoutineWaiting();
  routine->ReplyInquiry(CreateInquiryReply(
      mojom::CheckKeyboardBacklightStateReply::State::kAnyNotLitUp));
  observer.WaitUntilRoutineFinished();

  const auto& result = observer.state_;
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

TEST_F(KeyboardBacklightRoutineTest, ErrorWhenGetBrightness) {
  EXPECT_CALL(*mock_power_manager_proxy(),
              GetKeyboardBrightnessPercentAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(
          brillo::Error::Create(FROM_HERE, "", "", "").get()));

  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  routine->SetOnExceptionCallback(exception_future.GetCallback());
  routine->Start();

  EXPECT_EQ(exception_future.Get<1>(), "Failed to get brightness.");
}

TEST_F(KeyboardBacklightRoutineTest, ErrorWhenSetBrightnessWhenTesting) {
  ExpectInitialGetBrightness();
  EXPECT_CALL(*mock_power_manager_proxy(),
              SetKeyboardBrightnessAsync(_, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(
          brillo::Error::Create(FROM_HERE, "", "", "").get()));
  // ALS should be enabled anyway.
  EXPECT_CALL(*mock_power_manager_proxy(),
              SetKeyboardAmbientLightSensorEnabledAsync(_, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());

  auto routine_create = CreateRoutine();
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  routine->SetOnExceptionCallback(exception_future.GetCallback());

  RoutineObserverForTesting observer;
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();

  EXPECT_EQ(exception_future.Get<1>(), "Failed to set brightness.");
}

}  // namespace
}  // namespace diagnostics

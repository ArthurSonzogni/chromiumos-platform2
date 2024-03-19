// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/interactive_routine_control.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

mojom::RoutineInquiryReplyPtr CreateCheckLedLitUpStateReply() {
  return mojom::RoutineInquiryReply::NewCheckLedLitUpState(
      mojom::CheckLedLitUpStateReply::New());
}

mojom::RoutineInquiryPtr CreateCheckLedLitUpStateInquiry() {
  return mojom::RoutineInquiry::NewCheckLedLitUpState(
      mojom::CheckLedLitUpStateInquiry::New());
}

class FakeInteractiveRoutineControl final : public InteractiveRoutineControl {
 public:
  explicit FakeInteractiveRoutineControl(
      base::OnceCallback<void(uint32_t error, const std::string& reason)>
          on_exception_) {
    SetOnExceptionCallback(std::move(on_exception_));
  }
  FakeInteractiveRoutineControl(const FakeInteractiveRoutineControl&) = delete;
  FakeInteractiveRoutineControl& operator=(
      const FakeInteractiveRoutineControl&) = delete;
  ~FakeInteractiveRoutineControl() override = default;

  void OnStart() override { return; }

  void OnReplyInquiry(mojom::RoutineInquiryReplyPtr detail) override {
    last_reply_ = std::move(detail);
  }

  mojom::RoutineStatePtr GetStateSync() {
    base::test::TestFuture<mojom::RoutineStatePtr> future;
    GetState(future.GetCallback());
    return future.Take();
  }

  std::optional<mojom::RoutineInquiryReplyPtr> last_reply_;

  FRIEND_TEST(InteractiveRoutineControlTest,
              EnterWaitingInquiryStateFromRunning);
  FRIEND_TEST(InteractiveRoutineControlTest, ReplyInquirySuccessfully);
  FRIEND_TEST(InteractiveRoutineControlTest,
              ReplyInWaitingStateWithWrongDetailTypeCauseException);
};

// Test that state can successfully enter waiting with inquiry from running.
TEST(InteractiveRoutineControlTest, EnterWaitingInquiryStateFromRunning) {
  auto rc = FakeInteractiveRoutineControl(UnexpectedRoutineExceptionCallback());
  rc.Start();

  rc.SetWaitingInquiryState("Waiting Reason",
                            CreateCheckLedLitUpStateInquiry());
  auto state = rc.GetStateSync();
  EXPECT_EQ(state->percentage, 0);
  ASSERT_TRUE(state->state_union->is_waiting());
  EXPECT_EQ(state->state_union->get_waiting()->reason,
            mojom::RoutineStateWaiting::Reason::kWaitingInteraction);
  EXPECT_EQ(state->state_union->get_waiting()->message, "Waiting Reason");
  ASSERT_FALSE(state->state_union->get_waiting()->interaction.is_null());
  ASSERT_TRUE(state->state_union->get_waiting()->interaction->is_inquiry());
  ASSERT_FALSE(
      state->state_union->get_waiting()->interaction->get_inquiry().is_null());
  EXPECT_TRUE(state->state_union->get_waiting()
                  ->interaction->get_inquiry()
                  ->is_check_led_lit_up_state());
}

// Test that state can successfully be resumed when the reply and the inquiry
// matches.
TEST(InteractiveRoutineControlTest, ReplyInquirySuccessfully) {
  auto rc = FakeInteractiveRoutineControl(UnexpectedRoutineExceptionCallback());
  rc.Start();
  rc.SetWaitingInquiryState("", CreateCheckLedLitUpStateInquiry());
  auto expected_reply = CreateCheckLedLitUpStateReply();
  rc.ReplyInquiry(expected_reply->Clone());
  auto state = rc.GetStateSync();
  EXPECT_TRUE(state->state_union->is_running());
  EXPECT_EQ(rc.last_reply_, expected_reply);
}

// Test that calling `ReplyInquiry` in a non-waiting state results in an
// exception.
TEST(InteractiveRoutineControlTest, ReplyInNonWaitingStateCauseException) {
  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto rc = FakeInteractiveRoutineControl(exception_future.GetCallback());
  rc.Start();
  rc.ReplyInquiry(CreateCheckLedLitUpStateReply());
  EXPECT_EQ(rc.last_reply_, std::nullopt);
  auto [unused_error, reason] = exception_future.Get();
  EXPECT_EQ(reason, "Reply does not match the inquiry");
}

// Test that calling `ReplyInquiry` in the waiting state without an inquiry
// results in an exception.
TEST(InteractiveRoutineControlTest,
     ReplyInWaitingStateWithoutInquiryCauseException) {
  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto rc = FakeInteractiveRoutineControl(exception_future.GetCallback());
  rc.Start();
  rc.ReplyInquiry(CreateCheckLedLitUpStateReply());
  EXPECT_EQ(rc.last_reply_, std::nullopt);
  auto [unused_error, reason] = exception_future.Get();
  EXPECT_EQ(reason, "Reply does not match the inquiry");
}

// Test that replying with a wrong type to an inquiry results in an exception.
TEST(InteractiveRoutineControlTest,
     ReplyInWaitingStateWithWrongDetailTypeCauseException) {
  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto rc = FakeInteractiveRoutineControl(exception_future.GetCallback());
  rc.Start();
  rc.SetWaitingInquiryState("", CreateCheckLedLitUpStateInquiry());
  rc.ReplyInquiry(mojom::RoutineInquiryReply::NewUnrecognizedReply(false));
  EXPECT_EQ(rc.last_reply_, std::nullopt);
  auto [unused_error, reason] = exception_future.Get();
  EXPECT_EQ(reason, "Reply does not match the inquiry");
}

}  // namespace
}  // namespace diagnostics

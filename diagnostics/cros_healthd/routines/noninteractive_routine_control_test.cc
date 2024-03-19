// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"

#include <cstdint>
#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

class FakeNoninteractiveRoutineControl final
    : public NoninteractiveRoutineControl {
 public:
  explicit FakeNoninteractiveRoutineControl(
      base::OnceCallback<void(uint32_t error, const std::string& reason)>
          on_exception_) {
    SetOnExceptionCallback(std::move(on_exception_));
  }
  FakeNoninteractiveRoutineControl(const FakeNoninteractiveRoutineControl&) =
      delete;
  FakeNoninteractiveRoutineControl& operator=(
      const FakeNoninteractiveRoutineControl&) = delete;
  ~FakeNoninteractiveRoutineControl() override = default;

  void OnStart() override {}
};

TEST(NoninteractiveRoutineControlTest, ReplyInquiryCauseException) {
  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto rc = FakeNoninteractiveRoutineControl(exception_future.GetCallback());
  rc.Start();
  rc.ReplyInquiry(mojom::RoutineInquiryReply::NewCheckLedLitUpState(
      mojom::CheckLedLitUpStateReply::New()));
  auto [unused_error, reason] = exception_future.Get();
  EXPECT_EQ(reason, "Reply does not match the inquiry");
}

}  // namespace
}  // namespace diagnostics

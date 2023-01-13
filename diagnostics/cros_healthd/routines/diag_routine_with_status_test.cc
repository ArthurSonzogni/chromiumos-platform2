// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/diag_routine_with_status.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

class FakeRoutine : public DiagnosticRoutineWithStatus {
 public:
  FakeRoutine() = default;
  FakeRoutine(const FakeRoutine&) = delete;
  FakeRoutine& operator=(const FakeRoutine&) = delete;

  // DiagnosticRoutine overrides:
  //
  // These functions are implemented by derived classes so we don't need to
  // implement them in this test.
  ~FakeRoutine() = default;
  void Start() override {}
  void Resume() override {}
  void Cancel() override {}
  void PopulateStatusUpdate(mojom::RoutineUpdate* response,
                            bool include_output) override {}

  // Access the protected functions for testing.
  std::string GetStatusMessageForTesting() {
    return DiagnosticRoutineWithStatus::GetStatusMessage();
  }
  void UpdateStatusForTesting(mojom::DiagnosticRoutineStatusEnum status,
                              std::string message) {
    DiagnosticRoutineWithStatus::UpdateStatus(status, std::move(message));
  }
};

class DiagnosticRoutineWithStatusTest : public testing::Test {
 protected:
  FakeRoutine routine_;
};

TEST_F(DiagnosticRoutineWithStatusTest, CheckInitialState) {
  EXPECT_EQ(routine_.GetStatus(), mojom::DiagnosticRoutineStatusEnum::kReady);
  EXPECT_EQ(routine_.GetStatusMessageForTesting(), "");
}

TEST_F(DiagnosticRoutineWithStatusTest, UpdateStatus) {
  auto status = mojom::DiagnosticRoutineStatusEnum::kPassed;
  std::string status_message = "Fake routine passed.";
  routine_.UpdateStatusForTesting(status, status_message);
  EXPECT_EQ(routine_.GetStatus(), status);
  EXPECT_EQ(routine_.GetStatusMessageForTesting(), status_message);
}

}  // namespace
}  // namespace diagnostics

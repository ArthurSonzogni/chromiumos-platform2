// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/signal_strength.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fake/fake_network_diagnostics_routines.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;
using ::testing::_;
using ::testing::Values;
using ::testing::WithParamInterface;

// POD struct for SignalStrengthProblemTest.
struct SignalStrengthProblemTestParams {
  network_diagnostics_ipc::SignalStrengthProblem problem_enum;
  std::string failure_message;
};

class SignalStrengthRoutineTest : public testing::Test {
 public:
  SignalStrengthRoutineTest(const SignalStrengthRoutineTest&) = delete;
  SignalStrengthRoutineTest& operator=(const SignalStrengthRoutineTest&) =
      delete;

 protected:
  SignalStrengthRoutineTest() = default;

  void SetUp() override {
    fake_mojo_service()->InitializeFakeMojoService();
    routine_ = CreateSignalStrengthRoutine(fake_mojo_service());
  }

  mojom::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    CHECK(routine_);
    mojom::RoutineUpdate update{0, mojo::ScopedHandle(),
                                mojom::RoutineUpdateUnionPtr()};
    routine_->Start();
    task_environment_.RunUntilIdle();
    routine_->PopulateStatusUpdate(&update, true);
    return mojom::RoutineUpdate::New(update.progress_percent,
                                     std::move(update.output),
                                     std::move(update.routine_update_union));
  }

  FakeMojoService* fake_mojo_service() {
    return mock_context_.fake_mojo_service();
  }

  FakeNetworkDiagnosticsRoutines& fake_network_diagnostics_routines() {
    return fake_mojo_service()->fake_network_diagnostics_routines();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
};

// Test that the SignalStrength routine can be run successfully.
TEST_F(SignalStrengthRoutineTest, RoutineSuccess) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNoProblem,
      network_diagnostics_ipc::RoutineProblems::NewSignalStrengthProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kSignalStrengthRoutineNoProblemMessage);
}

// Test that the SignalStrength routine returns a kNotRun status when it is not
// run.
TEST_F(SignalStrengthRoutineTest, RoutineNotRun) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNotRun,
      network_diagnostics_ipc::RoutineProblems::NewSignalStrengthProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kSignalStrengthRoutineNotRunMessage);
}

// Test that the SignalStrength routine returns a kNotRun status when no remote
// is bound.
TEST_F(SignalStrengthRoutineTest, RemoteNotBound) {
  fake_mojo_service()->ResetNetworkDiagnosticsRoutines();
  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kSignalStrengthRoutineNotRunMessage);
}

// Tests that the SignalStrength routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the SignalStrengthProblemTestParams POD struct):
// * |problem_enum| - The type of SignalStrength problem.
// * |failure_message| - Failure message for a problem.
class SignalStrengthProblemTest
    : public SignalStrengthRoutineTest,
      public WithParamInterface<SignalStrengthProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  SignalStrengthProblemTestParams params() const { return GetParam(); }
};

// Test that the SignalStrength routine handles the given signal strength
// problem.
TEST_P(SignalStrengthProblemTest, HandleSignalStrengthProblem) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kProblem,
      network_diagnostics_ipc::RoutineProblems::NewSignalStrengthProblems(
          {params().problem_enum}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    SignalStrengthProblemTest,
    Values(SignalStrengthProblemTestParams{
        network_diagnostics_ipc::SignalStrengthProblem::kWeakSignal,
        kSignalStrengthRoutineWeakSignalProblemMessage}));

}  // namespace
}  // namespace diagnostics

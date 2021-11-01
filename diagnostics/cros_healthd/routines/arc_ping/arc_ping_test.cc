// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/optional.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_utils.h"
#include "diagnostics/cros_healthd/routines/arc_ping/arc_ping.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::Values;
using testing::WithArg;
using testing::WithParamInterface;

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// POD struct for ArcPingProblemTest.
struct ArcPingProblemTestParams {
  network_diagnostics_ipc::ArcPingProblem problem_enum;
  std::string failure_message;
};

class ArcPingRoutineTest : public testing::Test {
 protected:
  ArcPingRoutineTest() = default;
  ArcPingRoutineTest(const ArcPingRoutineTest&) = delete;
  ArcPingRoutineTest& operator=(const ArcPingRoutineTest&) = delete;

  void SetUp() override {
    routine_ = CreateArcPingRoutine(network_diagnostics_adapter());
  }

  mojo_ipc::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  MockNetworkDiagnosticsAdapter* network_diagnostics_adapter() {
    return mock_context_.network_diagnostics_adapter();
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
};

// Test that the ArcPing routine can be run successfully.
TEST_F(ArcPingRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunArcPingRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunArcPingCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            network_diagnostics_ipc::RoutineProblems::NewArcPingProblems({}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kArcPingRoutineNoProblemMessage);
}

// Test that the ArcPing routine returns a kNotRun status when it is
// not run.
TEST_F(ArcPingRoutineTest, RoutineNotRun) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunArcPingRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunArcPingCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            network_diagnostics_ipc::RoutineProblems::NewArcPingProblems({}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kArcPingRoutineNotRunMessage);
}

// Tests that the ArcPing routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the ArcPingProblemTestParams POD struct):
// * |problem_enum| - The type of ArcPing problem.
// * |failure_message| - Failure message for a problem.
class ArcPingProblemTest : public ArcPingRoutineTest,
                           public WithParamInterface<ArcPingProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  ArcPingProblemTestParams params() const { return GetParam(); }
};

// Test that the ArcPing routine handles the given gateway can be
// pinged problem.
TEST_P(ArcPingProblemTest, HandleArcPingProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunArcPingRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunArcPingCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kProblem,
            network_diagnostics_ipc::RoutineProblems::NewArcPingProblems(
                {params().problem_enum}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ArcPingProblemTest,
    Values(
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kFailedToGetArcServiceManager,
            kArcPingRoutineFailedToGetArcServiceManagerMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kFailedToGetNetInstanceForPingTest,
            kArcPingRoutineFailedToGetNetInstanceForPingTestMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kGetManagedPropertiesTimeoutFailure,
            kArcPingRoutineGetManagedPropertiesTimeoutFailureMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::kUnreachableGateway,
            kArcPingRoutineUnreachableGatewayMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kFailedToPingDefaultNetwork,
            kArcPingRoutineFailedToPingDefaultNetworkMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kDefaultNetworkAboveLatencyThreshold,
            kArcPingRoutineDefaultNetworkAboveLatencyThresholdMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kUnsuccessfulNonDefaultNetworksPings,
            kArcPingRoutineUnsuccessfulNonDefaultNetworksPingsMessage},
        ArcPingProblemTestParams{
            network_diagnostics_ipc::ArcPingProblem::
                kNonDefaultNetworksAboveLatencyThreshold,
            kArcPingRoutineNonDefaultNetworksAboveLatencyThresholdMessage}));

}  // namespace
}  // namespace diagnostics

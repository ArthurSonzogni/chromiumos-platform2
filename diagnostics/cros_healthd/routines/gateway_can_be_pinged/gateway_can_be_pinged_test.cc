// Copyright 2020 The Chromium OS Authors. All rights reserved.
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
#include "diagnostics/cros_healthd/routines/gateway_can_be_pinged/gateway_can_be_pinged.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

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

// POD struct for GatewayCanBePingedProblemTest.
struct GatewayCanBePingedProblemTestParams {
  network_diagnostics_ipc::GatewayCanBePingedProblem problem_enum;
  std::string failure_message;
};

}  // namespace

class GatewayCanBePingedRoutineTest : public testing::Test {
 protected:
  GatewayCanBePingedRoutineTest() = default;
  GatewayCanBePingedRoutineTest(const GatewayCanBePingedRoutineTest&) = delete;
  GatewayCanBePingedRoutineTest& operator=(
      const GatewayCanBePingedRoutineTest&) = delete;

  void SetUp() override {
    routine_ = CreateGatewayCanBePingedRoutine(network_diagnostics_adapter());
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

// Test that the GatewayCanBePinged routine can be run successfully.
TEST_F(GatewayCanBePingedRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunGatewayCanBePingedRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunGatewayCanBePingedCallback callback) {
        auto result =
            CreateResult(network_diagnostics_ipc::RoutineVerdict::kNoProblem,
                         network_diagnostics_ipc::RoutineProblems::
                             NewGatewayCanBePingedProblems({}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kPingRoutineNoProblemMessage);
}

// Test that the GatewayCanBePinged routine returns a kNotRun status when it is
// not run.
TEST_F(GatewayCanBePingedRoutineTest, RoutineNotRun) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunGatewayCanBePingedRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunGatewayCanBePingedCallback callback) {
        auto result =
            CreateResult(network_diagnostics_ipc::RoutineVerdict::kNotRun,
                         network_diagnostics_ipc::RoutineProblems::
                             NewGatewayCanBePingedProblems({}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kPingRoutineNotRunMessage);
}

// Tests that the GatewayCanBePinged routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the Gateway ProblemTestParams POD struct):
// * |problem_enum| - The type of GatewayCanBePinged problem.
// * |failure_message| - Failure message for a problem.
class GatewayCanBePingedProblemTest
    : public GatewayCanBePingedRoutineTest,
      public WithParamInterface<GatewayCanBePingedProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  GatewayCanBePingedProblemTestParams params() const { return GetParam(); }
};

// Test that the GatewayCanBePinged routine handles the given gateway can be
// pinged problem.
TEST_P(GatewayCanBePingedProblemTest, HandleGatewayCanBePingedProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunGatewayCanBePingedRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunGatewayCanBePingedCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kProblem,
            network_diagnostics_ipc::RoutineProblems::
                NewGatewayCanBePingedProblems({params().problem_enum}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    GatewayCanBePingedProblemTest,
    Values(
        GatewayCanBePingedProblemTestParams{
            network_diagnostics_ipc::GatewayCanBePingedProblem::
                kUnreachableGateway,
            kPingRoutineUnreachableGatewayProblemMessage},
        GatewayCanBePingedProblemTestParams{
            network_diagnostics_ipc::GatewayCanBePingedProblem::
                kFailedToPingDefaultNetwork,
            kPingRoutineFailedPingProblemMessage},
        GatewayCanBePingedProblemTestParams{
            network_diagnostics_ipc::GatewayCanBePingedProblem::
                kDefaultNetworkAboveLatencyThreshold,
            kPingRoutineHighPingLatencyProblemMessage},
        GatewayCanBePingedProblemTestParams{
            network_diagnostics_ipc::GatewayCanBePingedProblem::
                kUnsuccessfulNonDefaultNetworksPings,
            kPingRoutineFailedNonDefaultPingsProblemMessage},
        GatewayCanBePingedProblemTestParams{
            network_diagnostics_ipc::GatewayCanBePingedProblem::
                kNonDefaultNetworksAboveLatencyThreshold,
            kPingRoutineNonDefaultHighLatencyProblemMessage}));

}  // namespace diagnostics

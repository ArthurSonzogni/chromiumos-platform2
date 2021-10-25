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
#include "diagnostics/cros_healthd/routines/arc_http/arc_http.h"
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

// POD struct for ArcHttpProblemTest.
struct ArcHttpProblemTestParams {
  network_diagnostics_ipc::ArcHttpProblem problem_enum;
  std::string failure_message;
};

class ArcHttpRoutineTest : public testing::Test {
 protected:
  ArcHttpRoutineTest() = default;
  ArcHttpRoutineTest(const ArcHttpRoutineTest&) = delete;
  ArcHttpRoutineTest& operator=(const ArcHttpRoutineTest&) = delete;

  void SetUp() override {
    routine_ = CreateArcHttpRoutine(network_diagnostics_adapter());
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

// Test that the ArcHttp routine can be run successfully.
TEST_F(ArcHttpRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunArcHttpRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunArcHttpCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            network_diagnostics_ipc::RoutineProblems::NewArcHttpProblems({}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kArcHttpRoutineNoProblemMessage);
}

// Test that the ArcHttp routine returns an error when it is not run.
TEST_F(ArcHttpRoutineTest, RoutineError) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunArcHttpRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunArcHttpCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            network_diagnostics_ipc::RoutineProblems::NewArcHttpProblems({}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kArcHttpRoutineNotRunMessage);
}

// Tests that the ArcHttp routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the ArcHttpProblemTestParams POD struct):
// * |problem_enum| - The type of ArcHttp problem.
// * |failure_message| - Failure message for a problem.
class ArcHttpProblemTest : public ArcHttpRoutineTest,
                           public WithParamInterface<ArcHttpProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  ArcHttpProblemTestParams params() const { return GetParam(); }
};

// Test that the ArcHttp routine handles the given ARC HTTP problem.
TEST_P(ArcHttpProblemTest, HandleArcHttpProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunArcHttpRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunArcHttpCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kProblem,
            network_diagnostics_ipc::RoutineProblems::NewArcHttpProblems(
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
    ArcHttpProblemTest,
    Values(
        ArcHttpProblemTestParams{
            network_diagnostics_ipc::ArcHttpProblem::
                kFailedToGetArcServiceManager,
            kArcHttpRoutineFailedToGetArcServiceManagerMessage},
        ArcHttpProblemTestParams{
            network_diagnostics_ipc::ArcHttpProblem::
                kFailedToGetNetInstanceForHttpTest,
            kArcHttpRoutineFailedToGetNetInstanceForHttpTestMessage},
        ArcHttpProblemTestParams{
            network_diagnostics_ipc::ArcHttpProblem::kFailedHttpRequests,
            kArcHttpRoutineFailedHttpsRequestsProblemMessage},
        ArcHttpProblemTestParams{
            network_diagnostics_ipc::ArcHttpProblem::kHighLatency,
            kArcHttpRoutineHighLatencyProblemMessage},
        ArcHttpProblemTestParams{
            network_diagnostics_ipc::ArcHttpProblem::kVeryHighLatency,
            kArcHttpRoutineVeryHighLatencyProblemMessage}));

}  // namespace
}  // namespace diagnostics

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/video_conferencing.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/json/json_writer.h>
#include <base/run_loop.h>
#include <base/values.h>
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

// POD struct for VideoConferencingProblemTest.
struct VideoConferencingProblemTestParams {
  std::vector<network_diagnostics_ipc::VideoConferencingProblem> problems;
  std::string failure_message;
};

class VideoConferencingRoutineTest : public testing::Test {
 public:
  VideoConferencingRoutineTest(const VideoConferencingRoutineTest&) = delete;
  VideoConferencingRoutineTest& operator=(const VideoConferencingRoutineTest&) =
      delete;

 protected:
  VideoConferencingRoutineTest() = default;

  void SetUp() override {
    fake_mojo_service()->InitializeFakeMojoService();
    routine_ =
        CreateVideoConferencingRoutine(std::nullopt, fake_mojo_service());
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

// Test that the VideoConferencing routine can be run successfully.
TEST_F(VideoConferencingRoutineTest, RoutineSuccess) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNoProblem,
      network_diagnostics_ipc::RoutineProblems::NewVideoConferencingProblems(
          {}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kVideoConferencingRoutineNoProblemMessage);
}

// Test that the VideoConferencing routine returns an error when it is not
// run.
TEST_F(VideoConferencingRoutineTest, RoutineError) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNotRun,
      network_diagnostics_ipc::RoutineProblems::NewVideoConferencingProblems(
          {}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kVideoConferencingRoutineNotRunMessage);
}

// Test that the VideoConferencing routine returns a kNotRun status when no
// remote is bound.
TEST_F(VideoConferencingRoutineTest, RemoteNotBound) {
  fake_mojo_service()->ResetNetworkDiagnosticsRoutines();
  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kVideoConferencingRoutineNotRunMessage);
}

// Tests that the VideoConferencing routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the VideoConferencingProblemTestParams POD struct):
// * |problems| - The types of VideoConferencing problems.
// * |failure_message| - Failure message for a problem.
class VideoConferencingProblemTest
    : public VideoConferencingRoutineTest,
      public WithParamInterface<VideoConferencingProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  VideoConferencingProblemTestParams params() const { return GetParam(); }
};

// Test that the VideoConferencing routine handles the given video conferencing
// problem.
TEST_P(VideoConferencingProblemTest, HandleVideoConferencingProblem) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kProblem,
      network_diagnostics_ipc::RoutineProblems::NewVideoConferencingProblems(
          params().problems));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    VideoConferencingProblemTest,
    Values(
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kUdpFailure},
            kVideoConferencingRoutineUdpFailureProblemMessage},
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kTcpFailure},
            kVideoConferencingRoutineTcpFailureProblemMessage},
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kMediaFailure},
            kVideoConferencingRoutineMediaFailureProblemMessage},
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kUdpFailure,
             network_diagnostics_ipc::VideoConferencingProblem::kTcpFailure},
            kVideoConferencingRoutineUdpFailureProblemMessage +
                std::string("\n") +
                kVideoConferencingRoutineTcpFailureProblemMessage},
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kUdpFailure,
             network_diagnostics_ipc::VideoConferencingProblem::kMediaFailure},
            kVideoConferencingRoutineUdpFailureProblemMessage +
                std::string("\n") +
                kVideoConferencingRoutineMediaFailureProblemMessage},
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kTcpFailure,
             network_diagnostics_ipc::VideoConferencingProblem::kMediaFailure},
            kVideoConferencingRoutineTcpFailureProblemMessage +
                std::string("\n") +
                kVideoConferencingRoutineMediaFailureProblemMessage},
        VideoConferencingProblemTestParams{
            {network_diagnostics_ipc::VideoConferencingProblem::kUdpFailure,
             network_diagnostics_ipc::VideoConferencingProblem::kTcpFailure,
             network_diagnostics_ipc::VideoConferencingProblem::kMediaFailure},
            kVideoConferencingRoutineUdpFailureProblemMessage +
                std::string("\n") +
                kVideoConferencingRoutineTcpFailureProblemMessage +
                std::string("\n") +
                kVideoConferencingRoutineMediaFailureProblemMessage}));

}  // namespace
}  // namespace diagnostics

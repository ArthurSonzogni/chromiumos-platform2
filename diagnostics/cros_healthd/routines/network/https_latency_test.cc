// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/https_latency.h"

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

// POD struct for HttpsLatencyProblemTest.
struct HttpsLatencyProblemTestParams {
  network_diagnostics_ipc::HttpsLatencyProblem problem_enum;
  std::string failure_message;
};

class HttpsLatencyRoutineTest : public testing::Test {
 public:
  HttpsLatencyRoutineTest(const HttpsLatencyRoutineTest&) = delete;
  HttpsLatencyRoutineTest& operator=(const HttpsLatencyRoutineTest&) = delete;

 protected:
  HttpsLatencyRoutineTest() = default;

  void SetUp() override {
    fake_mojo_service()->InitializeFakeMojoService();
    routine_ = CreateHttpsLatencyRoutine(fake_mojo_service());
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

// Test that the HttpsLatency routine can be run successfully.
TEST_F(HttpsLatencyRoutineTest, RoutineSuccess) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNoProblem,
      network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kHttpsLatencyRoutineNoProblemMessage);
}

// Test that the HttpsLatency routine returns an error when it is not
// run.
TEST_F(HttpsLatencyRoutineTest, RoutineError) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNotRun,
      network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kHttpsLatencyRoutineNotRunMessage);
}

// Test that the HttpsLatency routine returns a kNotRun status when no remote is
// bound.
TEST_F(HttpsLatencyRoutineTest, RemoteNotBound) {
  fake_mojo_service()->ResetNetworkDiagnosticsRoutines();
  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kHttpsLatencyRoutineNotRunMessage);
}

// Tests that the HttpsLatency routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the HttpsLatencyProblemTestParams POD struct):
// * |problem_enum| - The type of HttpsLatency problem.
// * |failure_message| - Failure message for a problem.
class HttpsLatencyProblemTest
    : public HttpsLatencyRoutineTest,
      public WithParamInterface<HttpsLatencyProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  HttpsLatencyProblemTestParams params() const { return GetParam(); }
};

// Test that the HttpsLatency routine handles the given HTTPS latency problem.
TEST_P(HttpsLatencyProblemTest, HandleHttpsLatencyProblem) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kProblem,
      network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems(
          {params().problem_enum}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    HttpsLatencyProblemTest,
    Values(
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kFailedDnsResolutions,
            kHttpsLatencyRoutineFailedDnsResolutionsProblemMessage},
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kFailedHttpsRequests,
            kHttpsLatencyRoutineFailedHttpsRequestsProblemMessage},
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kHighLatency,
            kHttpsLatencyRoutineHighLatencyProblemMessage},
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kVeryHighLatency,
            kHttpsLatencyRoutineVeryHighLatencyProblemMessage}));

}  // namespace
}  // namespace diagnostics

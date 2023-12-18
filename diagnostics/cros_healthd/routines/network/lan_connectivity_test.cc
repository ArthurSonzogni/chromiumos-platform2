// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/lan_connectivity.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
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

class LanConnectivityRoutineTest : public testing::Test {
 public:
  LanConnectivityRoutineTest(const LanConnectivityRoutineTest&) = delete;
  LanConnectivityRoutineTest& operator=(const LanConnectivityRoutineTest&) =
      delete;

 protected:
  LanConnectivityRoutineTest() = default;

  void SetUp() override {
    fake_mojo_service()->InitializeFakeMojoService();
    routine_ = CreateLanConnectivityRoutine(fake_mojo_service());
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

// Test that the LanConnectivity routine returns
// cros_healthd::mojom::DiagnosticRoutineStatusEnum::kPassed when the the
// verdict is network_diagnostics::mojom::RoutineVerdict::kNoProblem.
TEST_F(LanConnectivityRoutineTest, RoutineSuccess) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNoProblem,
      network_diagnostics_ipc::RoutineProblems::NewLanConnectivityProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kLanConnectivityRoutineNoProblemMessage);
}

// Test that the LanConnectivity routine returns
// cros_healthd::mojom::DiagnosticRoutineStatusEnum::kFailed when the verdict is
// network_diagnostics::mojom::RoutineVerdict::kProblem.
TEST_F(LanConnectivityRoutineTest, RoutineFailed) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kProblem,
      network_diagnostics_ipc::RoutineProblems::NewLanConnectivityProblems(
          {network_diagnostics_ipc::LanConnectivityProblem::
               kNoLanConnectivity}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kLanConnectivityRoutineProblemMessage);
}

// Test that the LanConnectivity routine returns
// cros_healthd::mojom::DiagnosticRoutineStatusEnum::kNotRun when the
// routine is a network_diagnostics::mojom::RoutineVerdict::kNotRun.
TEST_F(LanConnectivityRoutineTest, RoutineNotRun) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNotRun,
      network_diagnostics_ipc::RoutineProblems::NewLanConnectivityProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kLanConnectivityRoutineNotRunMessage);
}

// Test that the LanConnectivity routine returns a kNotRun status when no remote
// is bound.
TEST_F(LanConnectivityRoutineTest, RemoteNotBound) {
  fake_mojo_service()->ResetNetworkDiagnosticsRoutines();
  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kLanConnectivityRoutineNotRunMessage);
}

}  // namespace
}  // namespace diagnostics

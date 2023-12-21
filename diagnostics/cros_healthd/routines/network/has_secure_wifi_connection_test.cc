// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/has_secure_wifi_connection.h"

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

// POD struct for HasSecureWiFiConnectionProblemTest.
struct HasSecureWiFiConnectionProblemTestParams {
  network_diagnostics_ipc::HasSecureWiFiConnectionProblem problem_enum;
  std::string failure_message;
};

class HasSecureWiFiConnectionRoutineTest : public testing::Test {
 public:
  HasSecureWiFiConnectionRoutineTest(
      const HasSecureWiFiConnectionRoutineTest&) = delete;
  HasSecureWiFiConnectionRoutineTest& operator=(
      const HasSecureWiFiConnectionRoutineTest&) = delete;

 protected:
  HasSecureWiFiConnectionRoutineTest() = default;

  void SetUp() override {
    fake_mojo_service()->InitializeFakeMojoService();
    routine_ = CreateHasSecureWiFiConnectionRoutine(fake_mojo_service());
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

// Test that the HasSecureWiFiConnection routine can be run successfully.
TEST_F(HasSecureWiFiConnectionRoutineTest, RoutineSuccess) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNoProblem,
      network_diagnostics_ipc::RoutineProblems::
          NewHasSecureWifiConnectionProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kHasSecureWiFiConnectionRoutineNoProblemMessage);
}

// Test that the HasSecureWiFiConnection routine returns a kNotRun status when
// it is not run.
TEST_F(HasSecureWiFiConnectionRoutineTest, RoutineNotRun) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kNotRun,
      network_diagnostics_ipc::RoutineProblems::
          NewHasSecureWifiConnectionProblems({}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kHasSecureWiFiConnectionRoutineNotRunMessage);
}

// Test that the HasSecureWiFiConnection routine returns a kNotRun status if no
// remote is bound.
TEST_F(HasSecureWiFiConnectionRoutineTest, RemoteNotBound) {
  fake_mojo_service()->ResetNetworkDiagnosticsRoutines();
  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kNotRun,
                             kHasSecureWiFiConnectionRoutineNotRunMessage);
}

// Tests that the HasSecureWiFiConnection routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the HasSecureWiFiConnectionProblemTestParams POD struct):
// * |problem_enum| - The type of HasSecureWiFiConnection problem.
// * |failure_message| - Failure message for a problem.
class HasSecureWiFiConnectionProblemTest
    : public HasSecureWiFiConnectionRoutineTest,
      public WithParamInterface<HasSecureWiFiConnectionProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  HasSecureWiFiConnectionProblemTestParams params() const { return GetParam(); }
};

// Test that the HasSecureWiFiConnection routine handles the given has secure
// WiFi connection problem.
TEST_P(HasSecureWiFiConnectionProblemTest,
       HandleHasSecureWiFiConnectionProblem) {
  fake_network_diagnostics_routines().SetRoutineResult(
      network_diagnostics_ipc::RoutineVerdict::kProblem,
      network_diagnostics_ipc::RoutineProblems::
          NewHasSecureWifiConnectionProblems({params().problem_enum}));

  mojom::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    HasSecureWiFiConnectionProblemTest,
    Values(
        HasSecureWiFiConnectionProblemTestParams{
            network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
                kSecurityTypeNone,
            kHasSecureWiFiConnectionRoutineSecurityTypeNoneProblemMessage},
        HasSecureWiFiConnectionProblemTestParams{
            network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
                kSecurityTypeWep8021x,
            kHasSecureWiFiConnectionRoutineSecurityTypeWep8021xProblemMessage},
        HasSecureWiFiConnectionProblemTestParams{
            network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
                kSecurityTypeWepPsk,
            kHasSecureWiFiConnectionRoutineSecurityTypeWepPskProblemMessage},
        HasSecureWiFiConnectionProblemTestParams{
            network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
                kUnknownSecurityType,
            kHasSecureWiFiConnectionRoutineUnknownSecurityTypeProblemMessage}));

}  // namespace
}  // namespace diagnostics

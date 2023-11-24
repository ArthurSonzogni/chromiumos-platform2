// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/camera/camera_availability.h"

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <chromeos/mojo/service_constants.h>
#include <gtest/gtest.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace service_manager_mojom = ::chromeos::mojo_service_manager::mojom;

// A helper function to create a RegisteredServiceState;
service_manager_mojom::ErrorOrServiceStatePtr NewRegisteredServiceState() {
  auto registered_service_state =
      service_manager_mojom::RegisteredServiceState::New();
  registered_service_state->owner =
      service_manager_mojom::ProcessIdentity::New();
  auto service_state = service_manager_mojom::ServiceState::NewRegisteredState(
      std::move(registered_service_state));
  auto error_or_service_state =
      service_manager_mojom::ErrorOrServiceState::NewState(
          std::move(service_state));
  return error_or_service_state;
}

// A helper function to create an UnregisteredServiceState;
service_manager_mojom::ErrorOrServiceStatePtr NewUnregisteredServiceState() {
  auto unregistered_service_state =
      service_manager_mojom::UnregisteredServiceState::New();
  auto service_state =
      service_manager_mojom::ServiceState::NewUnregisteredState(
          std::move(unregistered_service_state));
  auto error_or_service_state =
      service_manager_mojom::ErrorOrServiceState::NewState(
          std::move(service_state));
  return error_or_service_state;
}

// A helper function to create an Error of service manager.
service_manager_mojom::ErrorOrServiceStatePtr NewServiceManagerError() {
  return service_manager_mojom::ErrorOrServiceState::NewError(
      service_manager_mojom::Error::New());
}

mojom::RoutineStatePtr StartRoutineAndGetFinalState(
    std::unique_ptr<BaseRoutineControl>& routine) {
  base::RunLoop run_loop;
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer(run_loop.QuitClosure());
  routine->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine->Start();
  run_loop.Run();
  return observer.state_.Clone();
}

std::pair<uint32_t, std::string> StartRoutineAndWaitForException(
    std::unique_ptr<BaseRoutineControl>& routine) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  routine->SetOnExceptionCallback(future.GetCallback());
  routine->Start();
  return future.Take();
}

struct RoutineArgument {
  bool run_camera_service_available_check = false;
  bool run_camera_diagnostic_service_available_check = false;
};

class CameraAvailabilityRoutineTest : public testing::Test {
 public:
  CameraAvailabilityRoutineTest() = default;
  CameraAvailabilityRoutineTest(const CameraAvailabilityRoutineTest&) = delete;
  CameraAvailabilityRoutineTest& operator=(
      const CameraAvailabilityRoutineTest&) = delete;

 protected:
  void SetUp() override {
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();

    SetCameraServiceQueryResult(NewRegisteredServiceState());
    SetCameraDiagnosticServiceQueryResult(NewRegisteredServiceState());
  }

  std::unique_ptr<BaseRoutineControl> CreateRoutine(
      RoutineArgument argument_struct) {
    auto argument = mojom::CameraAvailabilityRoutineArgument::New();
    argument->run_camera_service_available_check =
        argument_struct.run_camera_service_available_check;
    argument->run_camera_diagnostic_service_available_check =
        argument_struct.run_camera_diagnostic_service_available_check;
    return std::make_unique<CameraAvailabilityRoutine>(&mock_context_,
                                                       std::move(argument));
  }

  void SetCameraServiceQueryResult(
      chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
          error_or_service_state) {
    fake_service_manager().SetQuery(chromeos::mojo_services::kCrosCameraService,
                                    std::move(error_or_service_state));
  }

  void SetCameraDiagnosticServiceQueryResult(
      chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
          error_or_service_state) {
    fake_service_manager().SetQuery(
        chromeos::mojo_services::kCrosCameraDiagnostics,
        std::move(error_or_service_state));
  }

  FakeServiceManager& fake_service_manager() {
    return mock_context_.fake_mojo_service()->fake_service_manager();
  }

  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<CameraAvailabilityRoutine> routine_;
};

TEST_F(CameraAvailabilityRoutineTest, PassedWhenAllSubtestsPassed) {
  SetCameraServiceQueryResult(NewRegisteredServiceState());
  SetCameraDiagnosticServiceQueryResult(NewRegisteredServiceState());

  auto routine =
      CreateRoutine({.run_camera_service_available_check = true,
                     .run_camera_diagnostic_service_available_check = true});

  const auto& result = StartRoutineAndGetFinalState(routine);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);

  const auto& detail = result->state_union->get_finished()->detail;
  ASSERT_TRUE(detail->is_camera_availability());
  EXPECT_EQ(detail->get_camera_availability()->camera_service_available_check,
            mojom::CameraSubtestResult::kPassed);
  EXPECT_EQ(detail->get_camera_availability()
                ->camera_diagnostic_service_available_check,
            mojom::CameraSubtestResult::kPassed);
}

TEST_F(CameraAvailabilityRoutineTest, FailedWhenCameraServiceSubtestFailed) {
  SetCameraServiceQueryResult(NewUnregisteredServiceState());

  auto routine = CreateRoutine({.run_camera_service_available_check = true});

  const auto& result = StartRoutineAndGetFinalState(routine);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);

  const auto& detail = result->state_union->get_finished()->detail;
  ASSERT_TRUE(detail->is_camera_availability());
  EXPECT_EQ(detail->get_camera_availability()->camera_service_available_check,
            mojom::CameraSubtestResult::kFailed);
}

TEST_F(CameraAvailabilityRoutineTest, CameraServiceSubtestNotRun) {
  auto routine = CreateRoutine({.run_camera_service_available_check = false});

  const auto& result = StartRoutineAndGetFinalState(routine);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& detail = result->state_union->get_finished()->detail;
  ASSERT_TRUE(detail->is_camera_availability());
  EXPECT_EQ(detail->get_camera_availability()->camera_service_available_check,
            mojom::CameraSubtestResult::kNotRun);
}

// The availability of the camera diagnostic service does not affect the main
// routine's passed/failed result.
TEST_F(CameraAvailabilityRoutineTest,
       PassedWhenCameraDiagnosticServiceSubtestFailed) {
  SetCameraServiceQueryResult(NewRegisteredServiceState());
  SetCameraDiagnosticServiceQueryResult(NewUnregisteredServiceState());

  auto routine =
      CreateRoutine({.run_camera_service_available_check = true,
                     .run_camera_diagnostic_service_available_check = true});

  const auto& result = StartRoutineAndGetFinalState(routine);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);

  const auto& detail = result->state_union->get_finished()->detail;
  ASSERT_TRUE(detail->is_camera_availability());
  EXPECT_EQ(detail->get_camera_availability()
                ->camera_diagnostic_service_available_check,
            mojom::CameraSubtestResult::kFailed);
}

TEST_F(CameraAvailabilityRoutineTest, CameraDiagnosticServiceSubtestNotRun) {
  auto routine =
      CreateRoutine({.run_camera_diagnostic_service_available_check = false});

  const auto& result = StartRoutineAndGetFinalState(routine);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& detail = result->state_union->get_finished()->detail;
  ASSERT_TRUE(detail->is_camera_availability());
  EXPECT_EQ(detail->get_camera_availability()
                ->camera_diagnostic_service_available_check,
            mojom::CameraSubtestResult::kNotRun);
}

TEST_F(CameraAvailabilityRoutineTest, ErrorWhenCameraServiceSubtestError) {
  SetCameraServiceQueryResult(NewServiceManagerError());

  auto routine = CreateRoutine({.run_camera_service_available_check = true});

  const auto& [unused_error, reason] = StartRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Error in mojo service state.");
}

TEST_F(CameraAvailabilityRoutineTest,
       ErrorWhenCameraDiagnosticServiceSubtestError) {
  SetCameraDiagnosticServiceQueryResult(NewServiceManagerError());

  auto routine =
      CreateRoutine({.run_camera_diagnostic_service_available_check = true});

  const auto& [unused_error, reason] = StartRoutineAndWaitForException(routine);
  EXPECT_EQ(reason, "Error in mojo service state.");
}

}  // namespace
}  // namespace diagnostics

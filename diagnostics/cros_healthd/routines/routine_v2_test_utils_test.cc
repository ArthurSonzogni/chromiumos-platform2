// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"

namespace diagnostics {
namespace {

// A routine that raises an exception when started.
class FakeExceptionRoutine final : public BaseRoutineControl {
 public:
  FakeExceptionRoutine() = default;
  FakeExceptionRoutine(const FakeExceptionRoutine&) = delete;
  FakeExceptionRoutine& operator=(const FakeExceptionRoutine&) = delete;
  ~FakeExceptionRoutine() = default;

  // BaseRoutineControl overrides:
  void OnStart() override { RaiseException("OnStart exception"); }
};

void StartFakeExceptionRoutineButDontExpectExceptions() {
  base::RunLoop run_loop;
  RoutineObserverForTesting observer{run_loop.QuitClosure()};

  FakeExceptionRoutine routine;
  routine.SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  routine.SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine.Start();
  run_loop.Run();
}

TEST(RoutineV2TestUtilsDeathTest,
     UnexpectedRoutineExceptionCallbackCheckDirectly) {
  EXPECT_DEATH(UnexpectedRoutineExceptionCallback().Run(
                   /*error*/ 0, /*reason*/ "error reason"),
               "An unexpected routine exception has occurred.*");
}

TEST(RoutineV2TestUtilsDeathTest,
     UnexpectedRoutineExceptionCallbackCheckInRunLoop) {
  base::test::TaskEnvironment task_environment;
  EXPECT_DEATH(StartFakeExceptionRoutineButDontExpectExceptions(),
               "An unexpected routine exception has occurred.*");
}

}  // namespace
}  // namespace diagnostics

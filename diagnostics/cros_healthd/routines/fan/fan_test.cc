// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/executor/executor.h"
#include "diagnostics/cros_healthd/routines/fan/fan.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;

class FanRoutineTest : public BaseFileTest {
 public:
  FanRoutineTest(const FanRoutineTest&) = delete;
  FanRoutineTest& operator=(const FanRoutineTest&) = delete;

 protected:
  FanRoutineTest() = default;

  void SetUp() {
    // Expect all tests to run reset fan control.
    EXPECT_CALL(*mock_context_.mock_executor(), SetAllFanAutoControl(_))
        .WillRepeatedly([=](Executor::SetAllFanAutoControlCallback callback) {
          std::move(callback).Run(std::nullopt);
        });
    // Defaults to 1 fan in setup.
    SetFanCrosConfig("1");
  }

  void SetupAndStartRoutine(bool passed, base::RunLoop* run_loop) {
    routine_->SetOnExceptionCallback(
        base::BindOnce([](uint32_t error, const std::string& reason) {
          ADD_FAILURE() << "An exception has occurred when it shouldn't have.";
        }));
    observer_ =
        std::make_unique<RoutineObserverForTesting>(run_loop->QuitClosure());
    routine_->SetObserver(observer_->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
  }

  void RunRoutineAndWaitForException() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(
        base::IgnoreArgs<uint32_t, const std::string&>(run_loop.QuitClosure()));
    routine_->Start();
    run_loop.Run();
  }

  void SetFanCrosConfig(const std::string& value) {
    SetFakeCrosConfig(paths::cros_config::kFanCount, value);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<BaseRoutineControl> routine_;
  std::unique_ptr<RoutineObserverForTesting> observer_;
};

// Test that the routine can pass if the fan speed is increased in the first
// `GetFanspeed` call.
TEST_F(FanRoutineTest, RoutineSuccessByFirstGetSpeedIncrease) {
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed + FanRoutine::kFanRpmChange},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to be increasing
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_THAT(fan_detail->passed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine can pass if the fan speed is increased in subsequent
// `GetFanspeed` call.
TEST_F(FanRoutineTest, RoutineSuccessByMultipleGetSpeedIncrease) {
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after increase.
        std::move(callback).Run({kFanSpeed + FanRoutine::kFanRpmDelta},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to be increasing
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_THAT(fan_detail->passed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine can pass if the fan speed can not be increased, but
// is decreased in the first `GetFanspeed` call.
TEST_F(FanRoutineTest, RoutineSuccessByFirstGetSpeedDecrease) {
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after decrease.
        std::move(callback).Run({kFanSpeed - FanRoutine::kFanRpmDelta},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have an increased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      })
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have a decreased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed - FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  // 3 updates for increase.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  // 1 update for decrease.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_THAT(fan_detail->passed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine can pass if the fan speed can not be increased, and
// is decreased after multiple `GetFanspeed` call.
TEST_F(FanRoutineTest, RoutineSuccessByMultipleGetSpeedDecrease) {
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after decrease.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after decrease.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after decrease.
        std::move(callback).Run({kFanSpeed - FanRoutine::kFanRpmDelta},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have an increased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      })
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have a decreased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed - FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  // 3 updates for increase.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  // 3 update for decrease.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_THAT(fan_detail->passed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine will report failure if the fan speed is not changed.
TEST_F(FanRoutineTest, RoutineFailureByNoFanSpeedChange) {
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after increase.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after decrease.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after decrease.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after decrease.
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have an increased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      })
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have a decreased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed - FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  // 3 updates for increase.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  // 3 update for decrease.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_EQ(fan_detail->passed_fan_ids.size(), 0);
  EXPECT_THAT(fan_detail->failed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine will report failure if the fan speed change is less
// than delta .
TEST_F(FanRoutineTest, RoutineFailureByChangeBelowDelta) {
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after increase.
        std::move(callback).Run({kFanSpeed + FanRoutine::kFanRpmDelta - 1},
                                std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after increase.
        std::move(callback).Run({kFanSpeed + FanRoutine::kFanRpmDelta - 1},
                                std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after increase.
        std::move(callback).Run({kFanSpeed + FanRoutine::kFanRpmDelta - 1},
                                std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after decrease.
        std::move(callback).Run({kFanSpeed - FanRoutine::kFanRpmDelta + 1},
                                std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after decrease.
        std::move(callback).Run({kFanSpeed - FanRoutine::kFanRpmDelta + 1},
                                std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after decrease.
        std::move(callback).Run({kFanSpeed - FanRoutine::kFanRpmDelta + 1},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have an increased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      })
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have a decreased fan speed.
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed - FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  // 3 updates for increase.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  // 3 update for decrease.
  task_environment_.FastForwardBy(FanRoutine::kFanRoutineUpdatePeriod * 3);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_EQ(fan_detail->passed_fan_ids.size(), 0);
  EXPECT_THAT(fan_detail->failed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
}

// Test that the routine will raise error if it encounters error from calling
// `GetAllFanSpeed`.
TEST_F(FanRoutineTest, RoutineExceptionByGetFanSpeedError) {
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({}, "Custom Error");
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  RunRoutineAndWaitForException();
}

// Test that the routine will raise error if it encounters error from calling
// `SetFanSpeed`.
TEST_F(FanRoutineTest, RoutineExceptionBySetFanSpeedError) {
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({0}, std::nullopt);
      });
  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to have an increased fan speed.
        std::move(callback).Run("custom error");
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  RunRoutineAndWaitForException();
}

// Test that the routine will pass with multiple fans.
TEST_F(FanRoutineTest, MultipleFanRoutineSuccess) {
  SetFanCrosConfig("2");
  constexpr int kFanSpeed1 = 1000;
  constexpr int kFanSpeed2 = 0;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed1, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed1 + FanRoutine::kFanRpmChange,
                                 kFanSpeed2 + FanRoutine::kFanRpmChange},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to be increasing
        EXPECT_THAT(
            fan_rpms,
            testing::UnorderedElementsAre(
                testing::Pair(0, kFanSpeed1 + FanRoutine::kFanRpmChange),
                testing::Pair(1, kFanSpeed2 + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_THAT(fan_detail->passed_fan_ids,
              testing::UnorderedElementsAreArray({0, 1}));
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine can have both passing and failing fans.
TEST_F(FanRoutineTest, MultipleFanRoutinePartialFailure) {
  SetFanCrosConfig("2");
  constexpr int kFanSpeed1 = 1000;
  constexpr int kFanSpeed2 = 0;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed1, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after increase.
        std::move(callback).Run(
            {kFanSpeed1 + FanRoutine::kFanRpmDelta, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after increase.
        std::move(callback).Run(
            {kFanSpeed1 + FanRoutine::kFanRpmDelta, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after increase.
        std::move(callback).Run(
            {kFanSpeed1 + FanRoutine::kFanRpmDelta, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // First response after decrease.
        std::move(callback).Run(
            {kFanSpeed1 + FanRoutine::kFanRpmDelta, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Second response after decrease.
        std::move(callback).Run(
            {kFanSpeed1 + FanRoutine::kFanRpmDelta, kFanSpeed2}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        // Third response after decrease.
        std::move(callback).Run(
            {kFanSpeed1 + FanRoutine::kFanRpmDelta, kFanSpeed2}, std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to be increasing
        EXPECT_THAT(
            fan_rpms,
            testing::UnorderedElementsAre(
                testing::Pair(0, kFanSpeed1 + FanRoutine::kFanRpmChange),
                testing::Pair(1, kFanSpeed2 + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      })
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to be increasing
        EXPECT_THAT(fan_rpms,
                    testing::UnorderedElementsAre(testing::Pair(1, 0)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_THAT(fan_detail->passed_fan_ids,
              testing::UnorderedElementsAreArray({0}));
  EXPECT_THAT(fan_detail->failed_fan_ids,
              testing::UnorderedElementsAreArray({1}));
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kMatched);
}

// Test that the routine will fail if there is no fan, but expected a fan.
TEST_F(FanRoutineTest, RoutineFailureByTooLittleFan) {
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({}, std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_EQ(fan_detail->passed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kNotMatched);
}

// Test that the routine will fail if there is more fan than expected.
TEST_F(FanRoutineTest, RoutineFailureByTooManyFan) {
  SetFanCrosConfig("3");
  constexpr int kFanSpeed = 1000;
  EXPECT_CALL(*mock_context_.mock_executor(), GetAllFanSpeed(_))
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed}, std::nullopt);
      })
      .WillOnce([=](Executor::GetAllFanSpeedCallback callback) {
        std::move(callback).Run({kFanSpeed + FanRoutine::kFanRpmChange},
                                std::nullopt);
      });

  EXPECT_CALL(*mock_context_.mock_executor(), SetFanSpeed(_, _))
      .WillOnce([=](const base::flat_map<uint8_t, uint16_t>& fan_rpms,
                    Executor::SetFanSpeedCallback callback) {
        // Set fan to be increasing
        EXPECT_THAT(fan_rpms, testing::UnorderedElementsAre(testing::Pair(
                                  0, kFanSpeed + FanRoutine::kFanRpmChange)));
        std::move(callback).Run(std::nullopt);
      });

  auto routine_create =
      FanRoutine::Create(&mock_context_, mojom::FanRoutineArgument::New());
  ASSERT_TRUE(routine_create.has_value());
  routine_ = std::move(routine_create.value());

  base::RunLoop run_loop;
  SetupAndStartRoutine(true, &run_loop);
  run_loop.Run();
  mojom::RoutineStatePtr result = std::move(observer_->state_);

  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  const auto& fan_detail =
      result->state_union->get_finished()->detail->get_fan();
  EXPECT_EQ(fan_detail->passed_fan_ids.size(), 1);
  EXPECT_EQ(fan_detail->failed_fan_ids.size(), 0);
  EXPECT_EQ(fan_detail->fan_count_status,
            mojom::HardwarePresenceStatus::kNotMatched);
}

}  // namespace
}  // namespace diagnostics

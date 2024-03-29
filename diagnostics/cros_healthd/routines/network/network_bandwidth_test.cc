// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/network_bandwidth.h"

#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::WithArgs;

constexpr char kTestOemName[] = "TEST_OEM_NAME";

class NetworkBandwidthRoutineTest : public BaseFileTest {
 public:
  NetworkBandwidthRoutineTest(const NetworkBandwidthRoutineTest&) = delete;
  NetworkBandwidthRoutineTest& operator=(const NetworkBandwidthRoutineTest&) =
      delete;

 protected:
  NetworkBandwidthRoutineTest() = default;

  void SetUp() override {
    SetFakeCrosConfig(paths::cros_config::kOemName, kTestOemName);
    auto routine_create = NetworkBandwidthRoutine::Create(&mock_context_);
    ASSERT_TRUE(routine_create.has_value());
    routine_ = std::move(routine_create.value());
  }

  // Setup the RunNetworkBandwidthTest call with the argument `type` to return
  // `average_speed`.
  void SetupRunBandwidthTest(mojom::NetworkBandwidthTestType type,
                             std::optional<double> average_speed) {
    EXPECT_CALL(*mock_executor(),
                RunNetworkBandwidthTest(type, kTestOemName, _, _, _))
        .WillOnce(base::test::RunOnceCallback<4>(average_speed));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    RoutineObserverForTesting observer;
    routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    observer.WaitUntilRoutineFinished();
    return std::move(observer.state_);
  }

  void RunRoutineAndWaitForException(const std::string& expected_reason) {
    base::test::TestFuture<uint32_t, const std::string&> future;
    routine_->SetOnExceptionCallback(future.GetCallback());
    routine_->Start();
    EXPECT_EQ(future.Get<std::string>(), expected_reason)
        << "Unexpected reason in exception.";
  }

  void VerifyRunningState(
      const mojom::RoutineStatePtr& state,
      uint8_t percentage,
      double speed_kbps,
      mojom::NetworkBandwidthRoutineRunningInfo::Type type) {
    EXPECT_EQ(state->percentage, percentage);
    ASSERT_TRUE(state->state_union->is_running());
    const auto& running = state->state_union->get_running();
    ASSERT_TRUE(running->info);
    ASSERT_TRUE(running->info->is_network_bandwidth());
    EXPECT_EQ(running->info->get_network_bandwidth()->speed_kbps, speed_kbps);
    EXPECT_EQ(running->info->get_network_bandwidth()->type, type);
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<BaseRoutineControl> routine_;
};

// Test that the network bandwidth routine can run successfully.
TEST_F(NetworkBandwidthRoutineTest, RoutineSuccess) {
  SetupRunBandwidthTest(mojom::NetworkBandwidthTestType::kDownload,
                        /*average_speed=*/123);
  SetupRunBandwidthTest(mojom::NetworkBandwidthTestType::kUpload,
                        /*average_speed=*/456);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  ASSERT_TRUE(state->detail->is_network_bandwidth());

  const auto& detail = state->detail->get_network_bandwidth();
  EXPECT_EQ(detail->download_speed_kbps, 123);
  EXPECT_EQ(detail->upload_speed_kbps, 456);
}

// Test that the network bandwidth routine handles the progress update.
TEST_F(NetworkBandwidthRoutineTest, RoutineProgressUpdate) {
  mojo::Remote<mojom::NetworkBandwidthObserver> download_remote, upload_remote;
  mojom::Executor::RunNetworkBandwidthTestCallback download_cb, upload_cb;
  EXPECT_CALL(*mock_executor(), RunNetworkBandwidthTest(
                                    mojom::NetworkBandwidthTestType::kDownload,
                                    kTestOemName, _, _, _))
      .WillOnce(WithArgs<2, 4>(
          [&](mojo::PendingRemote<mojom::NetworkBandwidthObserver> observer,
              mojom::Executor::RunNetworkBandwidthTestCallback callback) {
            download_remote.Bind(std::move(observer));
            download_remote->OnProgress(/*speed_kbps=*/321, /*percentage=*/50);
            download_remote.FlushForTesting();
            download_cb = std::move(callback);
          }));
  EXPECT_CALL(*mock_executor(),
              RunNetworkBandwidthTest(mojom::NetworkBandwidthTestType::kUpload,
                                      kTestOemName, _, _, _))
      .WillOnce(WithArgs<2, 4>(
          [&](mojo::PendingRemote<mojom::NetworkBandwidthObserver> observer,
              mojom::Executor::RunNetworkBandwidthTestCallback callback) {
            upload_remote.Bind(std::move(observer));
            upload_remote->OnProgress(/*speed_kbps=*/654, /*percentage=*/50);
            upload_remote.FlushForTesting();
            upload_cb = std::move(callback);
          }));

  routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer;
  routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine_->Start();

  observer.WaitRoutineRunningInfoUpdate();
  VerifyRunningState(
      observer.state_, /*percentage=*/25, /*speed_kbps=*/321,
      /*type=*/mojom::NetworkBandwidthRoutineRunningInfo::Type::kDownload);
  std::move(download_cb).Run(123);

  observer.WaitRoutineRunningInfoUpdate();
  VerifyRunningState(
      observer.state_, /*percentage=*/75, /*speed_kbps=*/654,
      /*type=*/mojom::NetworkBandwidthRoutineRunningInfo::Type::kUpload);
  std::move(upload_cb).Run(456);

  observer.WaitUntilRoutineFinished();
  EXPECT_TRUE(observer.state_->state_union->is_finished());

  const auto& state = observer.state_->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  ASSERT_TRUE(state->detail->is_network_bandwidth());

  const auto& detail = state->detail->get_network_bandwidth();
  EXPECT_EQ(detail->download_speed_kbps, 123);
  EXPECT_EQ(detail->upload_speed_kbps, 456);
}

// Test that the network bandwidth routine handles the error of running
// bandwidth test.
TEST_F(NetworkBandwidthRoutineTest, RoutineRunNetworkBandwidthTestError) {
  SetupRunBandwidthTest(mojom::NetworkBandwidthTestType::kDownload,
                        /*average_speed=*/123);
  SetupRunBandwidthTest(mojom::NetworkBandwidthTestType::kUpload,
                        /*average_speed=*/std::nullopt);

  RunRoutineAndWaitForException("Error running NDT");
}

// Test that the network bandwidth routine can handle the error when timeout
// occurred.
TEST_F(NetworkBandwidthRoutineTest, RoutineTimeoutOccurred) {
  EXPECT_CALL(*mock_executor(), RunNetworkBandwidthTest(_, _, _, _, _));

  RunRoutineAndWaitForException("Routine timeout");
}

// Test that the routine cannot be run if the device doesn't have the OEM name
// config.
TEST_F(NetworkBandwidthRoutineTest, RoutineUnsupportedWithoutOemName) {
  SetFakeCrosConfig(paths::cros_config::kOemName, std::nullopt);

  auto routine_create = NetworkBandwidthRoutine::Create(&mock_context_);
  ASSERT_FALSE(routine_create.has_value());
  EXPECT_TRUE(routine_create.error()->is_unsupported());
}

}  // namespace
}  // namespace diagnostics

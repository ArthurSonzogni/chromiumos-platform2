// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"
#include "rmad/utils/mock_cr50_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Eq;
using testing::Ne;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

namespace {

constexpr char kTestChallengeCode[] = "ABCDEFGH";
constexpr char kTestUnlockCode[] = "abcdefgh";
constexpr char kWrongUnlockCode[] = "aaa";
constexpr char kTestHwid[] = "MODEL TEST";
constexpr char kTestUrl[] =
    "https://www.google.com/chromeos/partner/console/"
    "cr50reset?challenge=ABCDEFGH&hwid=MODEL_TEST";

}  // namespace

class WriteProtectDisableRsuStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<WriteProtectDisableRsuStateHandler> CreateStateHandler(
      bool factory_mode_enabled,
      bool is_cros_debug,
      bool* reboot_called = nullptr) {
    // Mock |Cr50Utils|.
    auto mock_cr50_utils = std::make_unique<NiceMock<MockCr50Utils>>();
    ON_CALL(*mock_cr50_utils, IsFactoryModeEnabled())
        .WillByDefault(Return(factory_mode_enabled));
    ON_CALL(*mock_cr50_utils, GetRsuChallengeCode(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(kTestChallengeCode), Return(true)));
    ON_CALL(*mock_cr50_utils, PerformRsu(Eq(kTestUnlockCode)))
        .WillByDefault(Return(true));
    ON_CALL(*mock_cr50_utils, PerformRsu(Ne(kTestUnlockCode)))
        .WillByDefault(Return(false));
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils,
            GetString(Eq(CrosSystemUtils::kHwidProperty), _))
        .WillByDefault(DoAll(SetArgPointee<1>(kTestHwid), Return(true)));
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
        .WillByDefault(DoAll(SetArgPointee<1>(factory_mode_enabled ? 0 : 1),
                             Return(true)));
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kCrosDebugProperty), _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(is_cros_debug ? 1 : 0), Return(true)));

    // Register reboot EC callback.
    daemon_callback_->SetExecuteRebootEcCallback(
        base::BindRepeating(&WriteProtectDisableRsuStateHandlerTest::RebootEc,
                            base::Unretained(this), reboot_called));

    return base::MakeRefCounted<WriteProtectDisableRsuStateHandler>(
        json_store_, daemon_callback_, GetTempDirPath(),
        std::move(mock_cr50_utils), std::move(mock_crossystem_utils));
  }

  void RebootEc(bool* reboot_called, base::OnceCallback<void(bool)> callback) {
    if (reboot_called) {
      *reboot_called = true;
    }
    std::move(callback).Run(true);
  }

 protected:
  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       InitializeState_FactoryModeEnabled) {
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().rsu_done(), true);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().challenge_code(),
            kTestChallengeCode);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().hwid(), kTestHwid);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().challenge_url(), kTestUrl);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       InitializeState_FactoryModeDisabled) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().rsu_done(), false);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().challenge_code(),
            kTestChallengeCode);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().hwid(), kTestHwid);
  EXPECT_EQ(handler->GetState().wp_disable_rsu().challenge_url(), kTestUrl);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       GetNextStateCase_Success_Continue) {
  auto handler = CreateStateHandler(true, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_rsu(new WriteProtectDisableRsuState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
  EXPECT_FALSE(base::PathExists(
      GetTempDirPath().AppendASCII(kPowerwashRequestFilePath)));

  // Check |json_store_|.
  std::string wp_disable_method_name;
  WpDisableMethod wp_disable_method;
  EXPECT_TRUE(MetricsUtils::GetMetricsValue(json_store_, kWpDisableMethod,
                                            &wp_disable_method_name));
  EXPECT_TRUE(
      WpDisableMethod_Parse(wp_disable_method_name, &wp_disable_method));
  EXPECT_EQ(wp_disable_method, RMAD_WP_DISABLE_METHOD_RSU);
}

TEST_F(WriteProtectDisableRsuStateHandlerTest, GetNextStateCase_Success_Rsu) {
  bool reboot_called = false;
  auto handler = CreateStateHandler(false, true, &reboot_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wp_disable_rsu()->set_unlock_code(kTestUnlockCode);

  {
    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
    EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
    EXPECT_FALSE(reboot_called);
  }

  // A second call to |GetNextStateCase| before rebooting is fine.
  {
    auto [error, state_case] = handler->GetNextStateCase(state);
    EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
    EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
    EXPECT_FALSE(reboot_called);
  }

  task_environment_.FastForwardBy(
      WriteProtectDisableRsuStateHandler::kRebootDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_TRUE(base::PathExists(
      GetTempDirPath().AppendASCII(kPowerwashRequestFilePath)));
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       GetNextStateCase_PowerwashDisabled_CrosDebug) {
  bool reboot_called = false;
  auto handler = CreateStateHandler(false, true, &reboot_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  brillo::TouchFile(GetTempDirPath().AppendASCII(kDisablePowerwashFilePath));

  RmadState state;
  state.mutable_wp_disable_rsu()->set_unlock_code(kTestUnlockCode);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
  EXPECT_FALSE(reboot_called);

  task_environment_.FastForwardBy(
      WriteProtectDisableRsuStateHandler::kRebootDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(base::PathExists(
      GetTempDirPath().AppendASCII(kPowerwashRequestFilePath)));
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       GetNextStateCase_PowerwashDisabled_NonCrosDebug) {
  bool reboot_called = false;
  auto handler = CreateStateHandler(false, false, &reboot_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  brillo::TouchFile(GetTempDirPath().AppendASCII(kDisablePowerwashFilePath));

  RmadState state;
  state.mutable_wp_disable_rsu()->set_unlock_code(kTestUnlockCode);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
  EXPECT_FALSE(reboot_called);

  task_environment_.FastForwardBy(
      WriteProtectDisableRsuStateHandler::kRebootDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_TRUE(base::PathExists(
      GetTempDirPath().AppendASCII(kPowerwashRequestFilePath)));
}

TEST_F(WriteProtectDisableRsuStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisableRsuState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
  EXPECT_FALSE(base::PathExists(
      GetTempDirPath().AppendASCII(kPowerwashRequestFilePath)));
}

TEST_F(WriteProtectDisableRsuStateHandlerTest,
       GetNextStateCase_WrongUnlockCode) {
  auto handler = CreateStateHandler(false, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_wp_disable_rsu()->set_unlock_code(kWrongUnlockCode);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_CODE_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);
  EXPECT_FALSE(base::PathExists(
      GetTempDirPath().AppendASCII(kPowerwashRequestFilePath)));
}

}  // namespace rmad

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_ro_firmware_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/udev/mock_udev_device.h"
#include "rmad/udev/mock_udev_utils.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cmd_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_write_protect_utils.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kModel[] = "Model";

}  // namespace

namespace rmad {

class UpdateRoFirmwareStateHandlerTest : public StateHandlerTest {
 public:
  class SignalSender {
   public:
    MOCK_METHOD(void, SendRoFirmwareSignal, (UpdateRoFirmwareStatus), (const));
  };

  struct StateHandlerArgs {
    bool ro_verified = true;
    bool hwwp_enabled = false;
    std::optional<std::string> rmad_config_textproto = std::nullopt;
    bool copy_success = true;
    bool update_success = true;
  };

  scoped_refptr<UpdateRoFirmwareStateHandler> CreateStateHandler(
      const StateHandlerArgs& args) {
    json_store_->SetValue(kRoFirmwareVerified, args.ro_verified);

    // Register signal callback.
    daemon_callback_->SetUpdateRoFirmwareSignalCallback(
        base::BindRepeating(&SignalSender::SendRoFirmwareSignal,
                            base::Unretained(&signal_sender_)));
    daemon_callback_->SetExecuteCopyRootfsFirmwareUpdaterCallback(
        base::BindLambdaForTesting(
            [args](base::OnceCallback<void(bool)> callback) {
              std::move(callback).Run(args.copy_success);
            }));

    // Mock |UdevUtils|.
    auto mock_udev_utils = std::make_unique<NiceMock<MockUdevUtils>>();
    ON_CALL(*mock_udev_utils, EnumerateBlockDevices())
        .WillByDefault(Invoke(
            []() { return std::vector<std::unique_ptr<UdevDevice>>(); }));

    // Mock |CmdUtils|.
    auto mock_cmd_utils = std::make_unique<NiceMock<MockCmdUtils>>();

    // Mock |WriteProtectUtils|.
    auto mock_write_protect_utils =
        std::make_unique<NiceMock<MockWriteProtectUtils>>();
    ON_CALL(*mock_write_protect_utils, GetHardwareWriteProtectionStatus())
        .WillByDefault(Return(args.hwwp_enabled));

    // Mock |PowerManagerClient|.
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    ON_CALL(*mock_power_manager_client, Restart).WillByDefault(Return(true));

    auto mock_cros_config_utils =
        std::make_unique<NiceMock<MockCrosConfigUtils>>();
    ON_CALL(*mock_cros_config_utils, GetModelName(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kModel), Return(true)));

    if (args.rmad_config_textproto.has_value()) {
      base::FilePath textproto_dir = GetTempDirPath().Append(kModel);
      base::FilePath textproto_path =
          textproto_dir.Append("rmad_config.textproto");
      EXPECT_TRUE(base::CreateDirectory(textproto_dir));
      EXPECT_TRUE(
          base::WriteFile(textproto_path, args.rmad_config_textproto.value()));
    }

    return base::MakeRefCounted<UpdateRoFirmwareStateHandler>(
        json_store_, daemon_callback_, args.update_success, GetTempDirPath(),
        std::move(mock_udev_utils), std::move(mock_cmd_utils),
        std::move(mock_write_protect_utils),
        std::move(mock_power_manager_client),
        std::move(mock_cros_config_utils));
  }

  void ExpectSignal(UpdateRoFirmwareStatus expected_status) {
    EXPECT_CALL(signal_sender_, SendRoFirmwareSignal)
        .WillOnce(Invoke([expected_status](UpdateRoFirmwareStatus status) {
          EXPECT_EQ(status, expected_status);
        }));
    task_environment_.FastForwardBy(
        UpdateRoFirmwareStateHandler::kSignalInterval);
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(UpdateRoFirmwareStateHandlerTest, InitializeState_Success_RoVerified) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  UpdateRoFirmwareState state = handler->GetState().update_ro_firmware();
  EXPECT_EQ(state.optional(), true);
  EXPECT_EQ(state.skip_update_ro_firmware_from_rootfs(), false);
}

TEST_F(UpdateRoFirmwareStateHandlerTest,
       InitializeState_Success_RoNotVerified) {
  auto handler = CreateStateHandler({.ro_verified = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  UpdateRoFirmwareState state = handler->GetState().update_ro_firmware();
  EXPECT_EQ(state.optional(), false);
  EXPECT_EQ(state.skip_update_ro_firmware_from_rootfs(), false);
}

TEST_F(UpdateRoFirmwareStateHandlerTest,
       InitializeState_Success_RmadConfig_Empty) {
  constexpr char textproto[] = "";
  auto handler = CreateStateHandler({.rmad_config_textproto = textproto});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  UpdateRoFirmwareState state = handler->GetState().update_ro_firmware();
  EXPECT_EQ(state.skip_update_ro_firmware_from_rootfs(), false);
}

TEST_F(UpdateRoFirmwareStateHandlerTest,
       InitializeState_Success_RmadConfig_NonEmpty) {
  constexpr char textproto[] = R"(
skip_update_ro_firmware_from_rootfs: true
  )";
  auto handler = CreateStateHandler({.rmad_config_textproto = textproto});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  UpdateRoFirmwareState state = handler->GetState().update_ro_firmware();
  EXPECT_EQ(state.skip_update_ro_firmware_from_rootfs(), true);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, InitializeState_HwwpEnabled_Failed) {
  auto handler = CreateStateHandler({.hwwp_enabled = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_WP_ENABLED);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, RunState_SkipRootfs_WaitUsb) {
  constexpr char textproto[] = R"(
skip_update_ro_firmware_from_rootfs: true
  )";
  auto handler = CreateStateHandler({.rmad_config_textproto = textproto,
                                     .copy_success = true,
                                     .update_success = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  handler->RunState();
  ExpectSignal(RMAD_UPDATE_RO_FIRMWARE_WAIT_USB);

  bool firmware_updated;
  EXPECT_FALSE(json_store_->GetValue(kFirmwareUpdated, &firmware_updated) &&
               firmware_updated);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, RunState_Rootfs_Success) {
  auto handler =
      CreateStateHandler({.copy_success = true, .update_success = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  handler->RunState();
  ExpectSignal(RMAD_UPDATE_RO_FIRMWARE_REBOOTING);

  bool firmware_updated;
  EXPECT_TRUE(json_store_->GetValue(kFirmwareUpdated, &firmware_updated) &&
              firmware_updated);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, RunState_Rootfs_CopyFailed) {
  auto handler = CreateStateHandler({.copy_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  handler->RunState();
  ExpectSignal(RMAD_UPDATE_RO_FIRMWARE_FILE_NOT_FOUND);
  ExpectSignal(RMAD_UPDATE_RO_FIRMWARE_WAIT_USB);

  bool firmware_updated;
  EXPECT_FALSE(json_store_->GetValue(kFirmwareUpdated, &firmware_updated) &&
               firmware_updated);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, RunState_Rootfs_UpdateFailed) {
  auto handler =
      CreateStateHandler({.copy_success = true, .update_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  handler->RunState();
  ExpectSignal(RMAD_UPDATE_RO_FIRMWARE_FILE_NOT_FOUND);
  ExpectSignal(RMAD_UPDATE_RO_FIRMWARE_WAIT_USB);

  bool firmware_updated;
  EXPECT_FALSE(json_store_->GetValue(kFirmwareUpdated, &firmware_updated) &&
               firmware_updated);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Success_Skip) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_SKIP);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Success_Update) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No UpdateRoFirmwareState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

TEST_F(UpdateRoFirmwareStateHandlerTest, GetNextStateCase_Violation) {
  auto handler = CreateStateHandler({.ro_verified = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_ro_firmware = std::make_unique<UpdateRoFirmwareState>();
  update_ro_firmware->set_choice(
      UpdateRoFirmwareState::RMAD_UPDATE_CHOICE_SKIP);
  RmadState state;
  state.set_allocated_update_ro_firmware(update_ro_firmware.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);
}

}  // namespace rmad

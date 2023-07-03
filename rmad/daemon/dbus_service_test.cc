// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/daemon/dbus_service.h"

#include <sysexits.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/dbus/dbus_object_test_helpers.h>
#include <brillo/file_utils.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/rmad/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/interface/mock_rmad_interface.h"
#include "rmad/system/mock_tpm_manager_client.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::PopValueFromReader;
using testing::_;
using testing::A;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class DBusServiceTestBase : public testing::Test {
 public:
  struct RmadOptions {
    bool is_state_file_exist = false;
    RoVerificationStatus ro_verification_status =
        RMAD_RO_VERIFICATION_NOT_TRIGGERED;
    bool is_rmad_enabled_in_cros_config = true;
    std::string main_fw_type = "normal";
    bool rmad_setup_result = true;
    // Sets to std::nullopt makes getter return error.
    std::optional<bool> is_cros_debug = false;
    bool is_test_directory_exist = false;
  };

  DBusServiceTestBase() {
    dbus::Bus::Options options;
    mock_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    dbus::ObjectPath path(kRmadServicePath);
    mock_exported_object_ =
        base::MakeRefCounted<StrictMock<dbus::MockExportedObject>>(
            mock_bus_.get(), path);
    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));
    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .WillRepeatedly(Return());
    EXPECT_CALL(*mock_exported_object_, Unregister()).WillRepeatedly(Return());
  }
  ~DBusServiceTestBase() override = default;

  base::FilePath GetStateFilePath() const {
    return temp_dir_.GetPath().AppendASCII("state");
  }

  base::FilePath GetTestDirecotryPath() const {
    return temp_dir_.GetPath().AppendASCII(".test");
  }

  void StartDBusService(const RmadOptions& options) {
    auto mock_tpm_manager_client =
        std::make_unique<NiceMock<MockTpmManagerClient>>();
    auto mock_cros_config_utils =
        std::make_unique<NiceMock<MockCrosConfigUtils>>();
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();

    base::FilePath state_file_path = GetStateFilePath();
    if (options.is_state_file_exist) {
      brillo::TouchFile(state_file_path);
    }
    // It is ok to use file instead of directory here because we only check if
    // it exists.
    base::FilePath test_dir_path = GetTestDirecotryPath();
    if (options.is_test_directory_exist) {
      brillo::TouchFile(test_dir_path);
    }
    ON_CALL(*mock_tpm_manager_client, GetRoVerificationStatus(_))
        .WillByDefault(DoAll(SetArgPointee<0>(options.ro_verification_status),
                             Return(true)));
    ON_CALL(*mock_cros_config_utils, GetRmadConfig(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(RmadConfig(
                      {.enabled = options.is_rmad_enabled_in_cros_config})),
                  Return(true)));
    if (options.is_cros_debug.has_value()) {
      ON_CALL(*mock_crossystem_utils,
              GetInt(Eq(CrosSystemUtils::kCrosDebugProperty), _))
          .WillByDefault(DoAll(SetArgPointee<1>(options.is_cros_debug.value()),
                               Return(true)));
    } else {
      ON_CALL(*mock_crossystem_utils,
              GetString(Eq(CrosSystemUtils::kCrosDebugProperty), _))
          .WillByDefault(Return(false));
    }
    ON_CALL(*mock_crossystem_utils,
            GetString(Eq(CrosSystemUtils::kMainFwTypeProperty), _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(options.main_fw_type), Return(true)));
    ON_CALL(mock_rmad_service_, SetUp(_))
        .WillByDefault(Return(options.rmad_setup_result));

    dbus_service_ = std::make_unique<DBusService>(
        mock_bus_, &mock_rmad_service_, state_file_path, test_dir_path,
        std::move(mock_tpm_manager_client), std::move(mock_cros_config_utils),
        std::move(mock_crossystem_utils));
    ASSERT_EQ(dbus_service_->OnEventLoopStarted(), EX_OK);

    auto sequencer = base::MakeRefCounted<AsyncEventSequencer>();
    dbus_service_->RegisterDBusObjectsAsync(sequencer.get());
  }

  template <typename RequestProtobufType, typename ReplyProtobufType>
  void ExecuteMethod(const std::string& method_name,
                     const RequestProtobufType& request,
                     ReplyProtobufType* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    dbus::MessageWriter writer(call.get());
    writer.AppendProtoAsArrayOfBytes(request);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    if (response.get()) {
      dbus::MessageReader reader(response.get());
      EXPECT_TRUE(reader.PopArrayOfBytesAsProto(reply));
    }
  }

  template <typename ReplyProtobufType>
  void ExecuteMethod(const std::string& method_name,
                     const std::string request,
                     ReplyProtobufType* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    dbus::MessageWriter writer(call.get());
    writer.AppendString(request);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    if (response.get()) {
      dbus::MessageReader reader(response.get());
      EXPECT_TRUE(reader.PopArrayOfBytesAsProto(reply));
    }
  }

  template <typename ReplyProtobufType>
  void ExecuteMethod(const std::string& method_name, ReplyProtobufType* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    if (response.get()) {
      dbus::MessageReader reader(response.get());
      EXPECT_TRUE(reader.PopArrayOfBytesAsProto(reply));
    }
  }

  void ExecuteMethod(const std::string& method_name, std::string* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    if (response.get()) {
      dbus::MessageReader reader(response.get());
      EXPECT_TRUE(reader.PopString(reply));
    }
  }

  void ExecuteMethod(const std::string& method_name, bool* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    if (response.get()) {
      dbus::MessageReader reader(response.get());
      EXPECT_TRUE(reader.PopBool(reply));
    }
  }

  void SignalError(RmadErrorCode error) {
    dbus_service_->SendErrorSignal(error);
  }

  void SignalHardwareVerification(const HardwareVerificationResult& result) {
    dbus_service_->SendHardwareVerificationResultSignal(result);
  }

  void SignalUpdateRoFirmwareStatus(const UpdateRoFirmwareStatus status) {
    dbus_service_->SendUpdateRoFirmwareStatusSignal(status);
  }

  void SignalCalibrationOverall(CalibrationOverallStatus overall_status) {
    dbus_service_->SendCalibrationOverallSignal(overall_status);
  }

  void SignalCalibrationComponent(CalibrationComponentStatus component_status) {
    dbus_service_->SendCalibrationProgressSignal(component_status);
  }

  void SignalProvision(const ProvisionStatus& status) {
    dbus_service_->SendProvisionProgressSignal(status);
  }

  void SignalFinalize(const FinalizeStatus& status) {
    dbus_service_->SendFinalizeProgressSignal(status);
  }

  void SignalHardwareWriteProtection(bool enabled) {
    dbus_service_->SendHardwareWriteProtectionStateSignal(enabled);
  }

  void SignalPowerCableState(bool plugged_in) {
    dbus_service_->SendPowerCableStateSignal(plugged_in);
  }

  void SignalExternalDisk(bool detected) {
    dbus_service_->SendExternalDiskSignal(detected);
  }

  dbus::MockExportedObject* GetMockExportedObject() {
    return mock_exported_object_.get();
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  std::unique_ptr<dbus::MethodCall> CreateMethodCall(
      const std::string& method_name) {
    auto call =
        std::make_unique<dbus::MethodCall>(kRmadInterfaceName, method_name);
    call->SetSerial(1);
    return call;
  }

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  base::ScopedTempDir temp_dir_;
  StrictMock<MockRmadInterface> mock_rmad_service_;
  std::unique_ptr<DBusService> dbus_service_;
};

class DBusServiceIsRequiredTest : public DBusServiceTestBase {};

class DBusServiceTest : public DBusServiceTestBase {
 protected:
  void SetUp() override {
    DBusServiceTestBase::SetUp();
    EXPECT_CALL(mock_rmad_service_, SetUp(_)).Times(AnyNumber());
    EXPECT_CALL(mock_rmad_service_, TryTransitionNextStateFromCurrentState())
        .Times(AnyNumber());

    StartDBusService({.is_state_file_exist = true});
  }
};

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_NotRequired) {
  StartDBusService({.is_state_file_exist = false});
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, false);
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_RoVerificationPass) {
  StartDBusService({
      .ro_verification_status = RMAD_RO_VERIFICATION_PASS,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, true);
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));
}

TEST_F(DBusServiceIsRequiredTest,
       IsRmaRequired_RoVerificationUnsupportedTriggered) {
  StartDBusService({
      .ro_verification_status = RMAD_RO_VERIFICATION_UNSUPPORTED_TRIGGERED,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, true);
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_StateFileExists) {
  StartDBusService({.is_state_file_exist = true});
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, true);
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_InterfaceSetUpFailed) {
  StartDBusService({
      .is_state_file_exist = true,
      // The method call doesn't set up the interface so it works normally.
      .rmad_setup_result = false,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, true);
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_IsNotAllowed) {
  StartDBusService({
      .is_state_file_exist = true,
      .is_rmad_enabled_in_cros_config = false,  // Disable rmad by cros_config.
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, false);
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_IsNotNormalMode) {
  StartDBusService({
      .is_state_file_exist = true,
      .main_fw_type = "not_normal",  // Disable rmad by fw type.
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, false);
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_IsDevMode) {
  // Set cros debug and test directory to override the check of cros_config and
  // fw_type.
  StartDBusService({
      .is_state_file_exist = true,
      .is_rmad_enabled_in_cros_config = false,
      .main_fw_type = "not_normal",
      .is_cros_debug = true,
      .is_test_directory_exist = true,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, true);
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_IsNotDevModeNoCrosDebug) {
  // Failed to get cros_debug should not bypass the check of cros_config and
  // fw_type.
  StartDBusService({
      .is_state_file_exist = true,
      .is_rmad_enabled_in_cros_config = false,
      .main_fw_type = "not_normal",
      .is_cros_debug = std::nullopt,
      .is_test_directory_exist = true,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, false);
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_IsNotDevModeCrosDebugIsFalse) {
  // cros_debug sets to false should not bypass the check of cros_config and
  // fw_type.
  StartDBusService({
      .is_state_file_exist = true,
      .is_rmad_enabled_in_cros_config = false,
      .main_fw_type = "not_normal",
      .is_cros_debug = false,
      .is_test_directory_exist = true,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, false);
}

TEST_F(DBusServiceIsRequiredTest, IsRmaRequired_IsNotDevModeNoTestDir) {
  // No test directory should not bypass the check of cros_config and fw_type.
  StartDBusService({
      .is_state_file_exist = true,
      .is_rmad_enabled_in_cros_config = false,
      .main_fw_type = "not_normal",
      .is_cros_debug = true,
      .is_test_directory_exist = false,
  });
  bool is_rma_required;
  ExecuteMethod(kIsRmaRequiredMethod, &is_rma_required);
  EXPECT_EQ(is_rma_required, false);
}

TEST_F(DBusServiceIsRequiredTest, GetCurrentState_Success) {
  EXPECT_CALL(mock_rmad_service_, SetUp(_));
  EXPECT_CALL(mock_rmad_service_, TryTransitionNextStateFromCurrentState());
  EXPECT_CALL(mock_rmad_service_, GetCurrentState(_))
      .WillOnce(Invoke([](RmadInterface::GetStateCallback callback) {
        GetStateReply reply;
        reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
        std::move(callback).Run(reply, false);
      }));

  StartDBusService({.is_state_file_exist = true});
  GetStateReply reply;
  ExecuteMethod(kGetCurrentStateMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED, reply.error());
  EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
}

TEST_F(DBusServiceIsRequiredTest, GetCurrentState_RmaNotRequired) {
  EXPECT_CALL(mock_rmad_service_, SetUp(_)).Times(0);
  EXPECT_CALL(mock_rmad_service_, TryTransitionNextStateFromCurrentState())
      .Times(0);

  StartDBusService({.is_state_file_exist = false});
  GetStateReply reply;
  ExecuteMethod(kGetCurrentStateMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED, reply.error());
  EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
}

TEST_F(DBusServiceIsRequiredTest, GetCurrentState_InterfaceSetUpFailed) {
  EXPECT_CALL(mock_rmad_service_, SetUp(_));
  EXPECT_CALL(mock_rmad_service_, TryTransitionNextStateFromCurrentState())
      .Times(0);

  StartDBusService({
      .is_state_file_exist = true,
      .rmad_setup_result = false,
  });
  GetStateReply reply;
  ExecuteMethod(kGetCurrentStateMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_DAEMON_INITIALIZATION_FAILED, reply.error());
  EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
}

TEST_F(DBusServiceTest, TransitionNextState) {
  EXPECT_CALL(mock_rmad_service_, TransitionNextState(_, _))
      .WillOnce(Invoke([](const TransitionNextStateRequest& request,
                          RmadInterface::GetStateCallback callback) {
        GetStateReply reply;
        reply.set_error(RMAD_ERROR_OK);
        RmadState* state = new RmadState();
        state->set_allocated_welcome(new WelcomeState());
        reply.set_allocated_state(state);
        std::move(callback).Run(reply, false);
      }));

  TransitionNextStateRequest request;
  GetStateReply reply;
  ExecuteMethod(kTransitionNextStateMethod, request, &reply);
  EXPECT_EQ(RMAD_ERROR_OK, reply.error());
  EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
}

TEST_F(DBusServiceTest, TransitionPreviousState) {
  EXPECT_CALL(mock_rmad_service_, TransitionPreviousState(_))
      .WillOnce(Invoke([](RmadInterface::GetStateCallback callback) {
        GetStateReply reply;
        reply.set_error(RMAD_ERROR_TRANSITION_FAILED);
        std::move(callback).Run(reply, false);
      }));

  GetStateReply reply;
  ExecuteMethod(kTransitionPreviousStateMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_TRANSITION_FAILED, reply.error());
  EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
}

TEST_F(DBusServiceTest, AbortRma) {
  EXPECT_CALL(mock_rmad_service_, AbortRma(_))
      .WillOnce(Invoke([](RmadInterface::AbortRmaCallback callback) {
        AbortRmaReply reply;
        reply.set_error(RMAD_ERROR_ABORT_FAILED);
        std::move(callback).Run(reply, false);
      }));

  AbortRmaReply reply;
  ExecuteMethod(kAbortRmaMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_ABORT_FAILED, reply.error());
}

TEST_F(DBusServiceTest, GetLog) {
  EXPECT_CALL(mock_rmad_service_, GetLog(_))
      .WillOnce(Invoke([](RmadInterface::GetLogCallback callback) {
        GetLogReply reply;
        reply.set_error(RMAD_ERROR_OK);
        reply.set_log("RMA log");
        std::move(callback).Run(reply, false);
      }));

  GetLogReply reply;
  ExecuteMethod(kGetLogMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_OK, reply.error());
  EXPECT_EQ("RMA log", reply.log());
}

TEST_F(DBusServiceTest, SaveLog) {
  EXPECT_CALL(mock_rmad_service_, SaveLog(_, _))
      .WillOnce(Invoke([](const std::string& diagnostics_log_text,
                          RmadInterface::SaveLogCallback callback) {
        SaveLogReply reply;
        reply.set_error(RMAD_ERROR_OK);
        reply.set_save_path("/save/path");
        std::move(callback).Run(reply, false);
      }));

  const std::string text = "A sample diagnostics log.";
  SaveLogReply reply;
  ExecuteMethod(kSaveLogMethod, text, &reply);
  EXPECT_EQ(RMAD_ERROR_OK, reply.error());
  EXPECT_EQ("/save/path", reply.save_path());
}

TEST_F(DBusServiceTest, SignalError) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "Error");
        EXPECT_EQ("i", signal->GetSignature());
        dbus::MessageReader reader(signal);
        int error = 0;
        ASSERT_TRUE(brillo::dbus_utils::DBusType<int>::Read(&reader, &error));
        EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED,
                  static_cast<RmadErrorCode>(error));
      }));
  SignalError(RMAD_ERROR_RMA_NOT_REQUIRED);
}

TEST_F(DBusServiceTest, SignalHardwareVerification) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "HardwareVerificationResult");
        EXPECT_EQ("(bs)", signal->GetSignature());
        dbus::MessageReader reader(signal);
        std::tuple<bool, std::string> result;
        ASSERT_TRUE(
            (brillo::dbus_utils::DBusType<std::tuple<bool, std::string>>::Read(
                &reader, &result)));
        EXPECT_TRUE(std::get<0>(result));
        EXPECT_EQ("test_error_string", std::get<1>(result));
      }));
  HardwareVerificationResult result;
  result.set_is_compliant(true);
  result.set_error_str("test_error_string");
  SignalHardwareVerification(result);
}

TEST_F(DBusServiceTest, SignalUpdateRoFirmwareStatus) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "UpdateRoFirmwareStatus");
        EXPECT_EQ("i", signal->GetSignature());
        dbus::MessageReader reader(signal);
        int status;
        ASSERT_TRUE(brillo::dbus_utils::DBusType<int>::Read(&reader, &status));
        EXPECT_EQ(RMAD_UPDATE_RO_FIRMWARE_WAIT_USB,
                  static_cast<UpdateRoFirmwareStatus>(status));
      }));
  SignalUpdateRoFirmwareStatus(RMAD_UPDATE_RO_FIRMWARE_WAIT_USB);
}

TEST_F(DBusServiceTest, SignalCalibrationOverall) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "CalibrationOverall");
        EXPECT_EQ("i", signal->GetSignature());
        dbus::MessageReader reader(signal);
        int status;
        ASSERT_TRUE(brillo::dbus_utils::DBusType<int>::Read(&reader, &status));
        EXPECT_EQ(RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE,
                  static_cast<CalibrationOverallStatus>(status));
      }));
  SignalCalibrationOverall(RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);
}

TEST_F(DBusServiceTest, SignalCalibrationComponent) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "CalibrationProgress");
        EXPECT_EQ("(iid)", signal->GetSignature());
        dbus::MessageReader reader(signal);
        std::tuple<int, int, double> status;
        ASSERT_TRUE(
            (brillo::dbus_utils::DBusType<std::tuple<int, int, double>>::Read(
                &reader, &status)));
        EXPECT_EQ(RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER,
                  std::get<0>(status));
        EXPECT_EQ(CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS,
                  std::get<1>(status));
        EXPECT_DOUBLE_EQ(0.3, std::get<2>(status));
      }));
  CalibrationComponentStatus component_status;
  component_status.set_component(
      RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  component_status.set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  component_status.set_progress(0.3);
  SignalCalibrationComponent(component_status);
}

TEST_F(DBusServiceTest, SignalProvision) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "ProvisioningProgress");
        EXPECT_EQ("(idi)", signal->GetSignature());
        dbus::MessageReader reader(signal);
        std::tuple<int, double, int> status;
        ASSERT_TRUE(
            (brillo::dbus_utils::DBusType<std::tuple<int, double, int>>::Read(
                &reader, &status)));
        EXPECT_EQ(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS,
                  std::get<0>(status));
        EXPECT_DOUBLE_EQ(0.5, std::get<1>(status));
        EXPECT_EQ(ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL,
                  std::get<2>(status));
      }));
  ProvisionStatus status;
  status.set_status(ProvisionStatus::RMAD_PROVISION_STATUS_IN_PROGRESS);
  status.set_progress(0.5);
  status.set_error(ProvisionStatus::RMAD_PROVISION_ERROR_INTERNAL);
  SignalProvision(status);
}

TEST_F(DBusServiceTest, SignalFinalize) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "FinalizeProgress");
        EXPECT_EQ("(idi)", signal->GetSignature());
        dbus::MessageReader reader(signal);
        std::tuple<int, double, int> status;
        ASSERT_TRUE(
            (brillo::dbus_utils::DBusType<std::tuple<int, double, int>>::Read(
                &reader, &status)));
        EXPECT_EQ(FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS,
                  std::get<0>(status));
        EXPECT_DOUBLE_EQ(0.5, std::get<1>(status));
        EXPECT_EQ(FinalizeStatus::RMAD_FINALIZE_ERROR_INTERNAL,
                  std::get<2>(status));
      }));
  FinalizeStatus status;
  status.set_status(FinalizeStatus::RMAD_FINALIZE_STATUS_IN_PROGRESS);
  status.set_progress(0.5);
  status.set_error(FinalizeStatus::RMAD_FINALIZE_ERROR_INTERNAL);
  SignalFinalize(status);
}

TEST_F(DBusServiceTest, SignalHardwareWriteProtection) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "HardwareWriteProtectionState");
        EXPECT_EQ("b", signal->GetSignature());
        dbus::MessageReader reader(signal);
        bool wp_status = false;
        ASSERT_TRUE(
            brillo::dbus_utils::DBusType<bool>::Read(&reader, &wp_status));
        EXPECT_TRUE(wp_status);
      }));
  SignalHardwareWriteProtection(true);
}

TEST_F(DBusServiceTest, SignalPowerCableState) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "PowerCableState");
        EXPECT_EQ("b", signal->GetSignature());
        dbus::MessageReader reader(signal);
        bool power_cable_status = false;
        ASSERT_TRUE(brillo::dbus_utils::DBusType<bool>::Read(
            &reader, &power_cable_status));
        EXPECT_TRUE(power_cable_status);
      }));
  SignalPowerCableState(true);
}

TEST_F(DBusServiceTest, SignalExternalDisk) {
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillOnce(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "ExternalDiskDetected");
        EXPECT_EQ("b", signal->GetSignature());
        dbus::MessageReader reader(signal);
        bool external_disk_status = false;
        ASSERT_TRUE(brillo::dbus_utils::DBusType<bool>::Read(
            &reader, &external_disk_status));
        EXPECT_TRUE(external_disk_status);
      }));
  SignalExternalDisk(true);
}

}  // namespace rmad

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <brillo/dbus/dbus_object_test_helpers.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/rmad/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/dbus_service.h"
#include "rmad/mock_rmad_interface.h"

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::PopValueFromReader;
using testing::_;
using testing::A;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class DBusServiceTest : public testing::Test {
 public:
  DBusServiceTest() {
    dbus::Bus::Options options;
    mock_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    dbus::ObjectPath path(kRmadServicePath);
    mock_exported_object_ =
        base::MakeRefCounted<NiceMock<dbus::MockExportedObject>>(
            mock_bus_.get(), path);
    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));
    dbus_service_ =
        std::make_unique<DBusService>(mock_bus_, &mock_rmad_service_);

    EXPECT_CALL(mock_rmad_service_, GetCurrentStateCase())
        .WillRepeatedly(Return(RmadState::STATE_NOT_SET));
    EXPECT_CALL(
        mock_rmad_service_,
        RegisterSignalSender(
            _, A<std::unique_ptr<base::RepeatingCallback<bool(bool)>>>()))
        .WillRepeatedly(Return());
    EXPECT_CALL(
        mock_rmad_service_,
        RegisterSignalSender(_, A<std::unique_ptr<base::RepeatingCallback<bool(
                                    const HardwareVerificationResult&)>>>()))
        .WillRepeatedly(Return());
    EXPECT_CALL(
        mock_rmad_service_,
        RegisterSignalSender(
            _,
            A<std::unique_ptr<
                base::RepeatingCallback<bool(CalibrationSetupInstruction)>>>()))
        .WillRepeatedly(Return());
    EXPECT_CALL(
        mock_rmad_service_,
        RegisterSignalSender(
            _, A<std::unique_ptr<
                   base::RepeatingCallback<bool(CalibrationOverallStatus)>>>()))
        .WillRepeatedly(Return());
    EXPECT_CALL(
        mock_rmad_service_,
        RegisterSignalSender(
            _,
            A<std::unique_ptr<
                base::RepeatingCallback<bool(CalibrationComponentStatus)>>>()))
        .WillRepeatedly(Return());
  }
  ~DBusServiceTest() override = default;

  void RegisterDBusObjectAsync() {
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
    dbus::MessageReader reader(response.get());
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(reply));
  }

  template <typename ReplyProtobufType>
  void ExecuteMethod(const std::string& method_name, ReplyProtobufType* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    dbus::MessageReader reader(response.get());
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(reply));
  }

  void ExecuteMethod(const std::string& method_name, std::string* reply) {
    std::unique_ptr<dbus::MethodCall> call = CreateMethodCall(method_name);
    auto response = brillo::dbus_utils::testing::CallMethod(
        *dbus_service_->dbus_object_, call.get());
    dbus::MessageReader reader(response.get());
    EXPECT_TRUE(reader.PopString(reply));
  }

  bool SignalError(RmadErrorCode error) {
    return dbus_service_->SendErrorSignal(error);
  }

  bool SignalHardwareVerification(const HardwareVerificationResult& result) {
    return dbus_service_->SendHardwareVerificationResultSignal(result);
  }

  bool SignalCalibrationSetup(CalibrationSetupInstruction setup_instruction) {
    return dbus_service_->SendCalibrationSetupSignal(setup_instruction);
  }

  bool SignalCalibrationOverall(CalibrationOverallStatus overall_status) {
    return dbus_service_->SendCalibrationOverallSignal(overall_status);
  }

  bool SignalCalibrationComponent(CalibrationComponentStatus component_status) {
    return dbus_service_->SendCalibrationProgressSignal(component_status);
  }

  bool SignalProvisioning(ProvisionDeviceState::ProvisioningStep step,
                          double progress) {
    return dbus_service_->SendProvisioningProgressSignal(step, progress);
  }

  bool SignalHardwareWriteProtection(bool enabled) {
    return dbus_service_->SendHardwareWriteProtectionStateSignal(enabled);
  }

  bool SignalPowerCable(bool plugged_in) {
    return dbus_service_->SendPowerCableStateSignal(plugged_in);
  }

  dbus::MockExportedObject* GetMockExportedObject() {
    return mock_exported_object_.get();
  }

 protected:
  std::unique_ptr<dbus::MethodCall> CreateMethodCall(
      const std::string& method_name) {
    auto call =
        std::make_unique<dbus::MethodCall>(kRmadInterfaceName, method_name);
    call->SetSerial(1);
    return call;
  }

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  StrictMock<MockRmadInterface> mock_rmad_service_;
  std::unique_ptr<DBusService> dbus_service_;
};

TEST_F(DBusServiceTest, GetCurrentState) {
  RegisterDBusObjectAsync();

  EXPECT_CALL(mock_rmad_service_, GetCurrentState(_))
      .WillOnce(Invoke([](const RmadInterface::GetStateCallback& callback) {
        GetStateReply reply;
        reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
        callback.Run(reply);
      }));

  GetStateReply reply;
  ExecuteMethod(kGetCurrentStateMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED, reply.error());
  EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
}

TEST_F(DBusServiceTest, TransitionNextState) {
  RegisterDBusObjectAsync();

  EXPECT_CALL(mock_rmad_service_, TransitionNextState(_, _))
      .WillOnce(Invoke([](const TransitionNextStateRequest& request,
                          const RmadInterface::GetStateCallback& callback) {
        GetStateReply reply;
        reply.set_error(RMAD_ERROR_OK);
        RmadState* state = new RmadState();
        state->set_allocated_welcome(new WelcomeState());
        reply.set_allocated_state(state);
        callback.Run(reply);
      }));

  TransitionNextStateRequest request;
  GetStateReply reply;
  ExecuteMethod(kTransitionNextStateMethod, request, &reply);
  EXPECT_EQ(RMAD_ERROR_OK, reply.error());
  EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
}

TEST_F(DBusServiceTest, TransitionPreviousState) {
  RegisterDBusObjectAsync();

  EXPECT_CALL(mock_rmad_service_, TransitionPreviousState(_))
      .WillOnce(Invoke([](const RmadInterface::GetStateCallback& callback) {
        GetStateReply reply;
        reply.set_error(RMAD_ERROR_TRANSITION_FAILED);
        callback.Run(reply);
      }));

  GetStateReply reply;
  ExecuteMethod(kTransitionPreviousStateMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_TRANSITION_FAILED, reply.error());
  EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
}

TEST_F(DBusServiceTest, AbortRma) {
  RegisterDBusObjectAsync();

  EXPECT_CALL(mock_rmad_service_, AbortRma(_))
      .WillOnce(Invoke([](const RmadInterface::AbortRmaCallback& callback) {
        AbortRmaReply reply;
        reply.set_error(RMAD_ERROR_ABORT_FAILED);
        callback.Run(reply);
      }));

  AbortRmaReply reply;
  ExecuteMethod(kAbortRmaMethod, &reply);
  EXPECT_EQ(RMAD_ERROR_ABORT_FAILED, reply.error());
}

TEST_F(DBusServiceTest, GetLogPath) {
  // This method doesn't call |mock_rma_service_|.
  RegisterDBusObjectAsync();

  std::string reply;
  ExecuteMethod(kGetLogPathMethod, &reply);
  EXPECT_EQ("not_supported", reply);
}

TEST_F(DBusServiceTest, SignalError) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "Error");
        dbus::MessageReader reader(signal);
        int error;
        EXPECT_TRUE(reader.PopInt32(&error));
        EXPECT_EQ(error, RMAD_ERROR_RMA_NOT_REQUIRED);
      }));
  EXPECT_TRUE(SignalError(RMAD_ERROR_RMA_NOT_REQUIRED));
}

TEST_F(DBusServiceTest, SignalHardwareVerification) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "HardwareVerificationResult");
        dbus::MessageReader reader(signal);
        HardwareVerificationResult result;
        EXPECT_TRUE(PopValueFromReader(&reader, &result));
        EXPECT_EQ(result.is_compliant(), true);
        EXPECT_EQ(result.error_str(), "test_error_string");
      }));
  HardwareVerificationResult result;
  result.set_is_compliant(true);
  result.set_error_str("test_error_string");
  EXPECT_TRUE(SignalHardwareVerification(result));
}

TEST_F(DBusServiceTest, SignalCalibrationSetup) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "CalibrationSetup");
        dbus::MessageReader reader(signal);
        int setup_instruction;
        EXPECT_TRUE(reader.PopInt32(&setup_instruction));
        EXPECT_EQ(setup_instruction,
                  RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE);
      }));
  EXPECT_TRUE(SignalCalibrationSetup(
      RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE));
}

TEST_F(DBusServiceTest, SignalCalibrationOverall) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "CalibrationOverall");
        dbus::MessageReader reader(signal);
        int overall_status;
        EXPECT_TRUE(reader.PopInt32(&overall_status));
        EXPECT_EQ(overall_status,
                  RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);
      }));
  EXPECT_TRUE(SignalCalibrationOverall(
      RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE));
}

TEST_F(DBusServiceTest, SignalCalibrationComponent) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "CalibrationProgress");
        dbus::MessageReader reader(signal);
        CalibrationComponentStatus calibration_status;
        EXPECT_TRUE(PopValueFromReader(&reader, &calibration_status));
        EXPECT_EQ(calibration_status.component(),
                  RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
        EXPECT_EQ(calibration_status.status(),
                  CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
        EXPECT_EQ(calibration_status.progress(), 0.3);
      }));
  CalibrationComponentStatus component_status;
  component_status.set_component(
      RmadComponent::RMAD_COMPONENT_BASE_ACCELEROMETER);
  component_status.set_status(
      CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  component_status.set_progress(0.3);
  EXPECT_TRUE(SignalCalibrationComponent(component_status));
}

TEST_F(DBusServiceTest, SignalProvisioning) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "ProvisioningProgress");
        dbus::MessageReader reader(signal);
        int step;
        double progress;
        EXPECT_TRUE(reader.PopInt32(&step));
        EXPECT_TRUE(reader.PopDouble(&progress));
        EXPECT_EQ(step,
                  ProvisionDeviceState::RMAD_PROVISIONING_STEP_IN_PROGRESS);
        EXPECT_EQ(progress, 0.63);
      }));
  EXPECT_TRUE(SignalProvisioning(
      ProvisionDeviceState::RMAD_PROVISIONING_STEP_IN_PROGRESS, 0.63));
}

TEST_F(DBusServiceTest, SignalHardwareWriteProtection) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "HardwareWriteProtectionState");
        dbus::MessageReader reader(signal);
        bool enabled;
        EXPECT_TRUE(reader.PopBool(&enabled));
        EXPECT_TRUE(enabled);
      }));
  EXPECT_TRUE(SignalHardwareWriteProtection(true));
}

TEST_F(DBusServiceTest, SignalPowerCable) {
  RegisterDBusObjectAsync();
  EXPECT_CALL(*GetMockExportedObject(), SendSignal(_))
      .WillRepeatedly(Invoke([](dbus::Signal* signal) {
        EXPECT_EQ(signal->GetInterface(), "org.chromium.Rmad");
        EXPECT_EQ(signal->GetMember(), "PowerCableState");
        dbus::MessageReader reader(signal);
        bool plugged_in;
        EXPECT_TRUE(reader.PopBool(&plugged_in));
        EXPECT_TRUE(plugged_in);
      }));
  EXPECT_TRUE(SignalPowerCable(true));
}

}  // namespace rmad

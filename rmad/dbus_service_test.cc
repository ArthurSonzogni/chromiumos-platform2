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
using testing::_;
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

  EXPECT_CALL(mock_rmad_service_, GetCurrentState(_, _))
      .WillOnce(
          Invoke([](const GetCurrentStateRequest& request,
                    const RmadInterface::GetCurrentStateCallback& callback) {
            GetCurrentStateReply reply;
            reply.set_state(RMAD_STATE_RMA_NOT_REQUIRED);
            callback.Run(reply);
          }));

  GetCurrentStateRequest request;
  GetCurrentStateReply reply;
  ExecuteMethod(kGetCurrentStateMethod, request, &reply);
  EXPECT_EQ(RMAD_STATE_RMA_NOT_REQUIRED, reply.state());
}

TEST_F(DBusServiceTest, TransitionState) {
  RegisterDBusObjectAsync();

  EXPECT_CALL(mock_rmad_service_, TransitionState(_, _))
      .WillOnce(
          Invoke([](const TransitionStateRequest& request,
                    const RmadInterface::TransitionStateCallback& callback) {
            TransitionStateReply reply;
            reply.set_state(RMAD_STATE_RMA_NOT_REQUIRED);
            callback.Run(reply);
          }));

  TransitionStateRequest request;
  TransitionStateReply reply;
  ExecuteMethod(kTransitionStateMethod, request, &reply);
  EXPECT_EQ(RMAD_STATE_RMA_NOT_REQUIRED, reply.state());
}

}  // namespace rmad

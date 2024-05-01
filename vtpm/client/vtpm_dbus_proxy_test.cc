// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/client/vtpm_dbus_proxy.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/threading/thread.h>
#include <dbus/error.h>
#include <dbus/object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/command_codes.h"
#include "trunks/error_codes.h"
#include "trunks/mock_dbus_bus.h"
#include "vtpm/dbus_interface.h"
#include "vtpm/vtpm_interface.pb.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace {

class FakeObjectProxy : public dbus::ObjectProxy {
 public:
  FakeObjectProxy()
      : dbus::ObjectProxy(
            nullptr, "", dbus::ObjectPath(vtpm::kVtpmServicePath), 0) {}

  void CallMethodWithErrorCallback(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ObjectProxy::ResponseCallback callback,
      dbus::ObjectProxy::ErrorCallback error_callback) override {
    base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response =
        CallMethodAndBlock(method_call, timeout_ms);
    if (response.has_value() && response.value()) {
      std::move(callback).Run(response.value().get());
    } else {
      method_call->SetSerial(1);
      std::unique_ptr<dbus::ErrorResponse> error_response =
          dbus::ErrorResponse::FromMethodCall(method_call, "org.MyError",
                                              "Error message");
      std::move(error_callback).Run(error_response.get());
    }
  }

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error>
  CallMethodAndBlock(dbus::MethodCall* method_call,
                     int /* timeout_ms */) override {
    dbus::MessageReader reader(method_call);
    vtpm::SendCommandRequest command_proto;
    brillo::dbus_utils::ReadDBusArgs(&reader, &command_proto);
    last_command_ = command_proto.command();
    if (next_response_.empty()) {
      return base::unexpected(dbus::Error());
    }
    std::unique_ptr<dbus::Response> dbus_response =
        dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(dbus_response.get());
    vtpm::SendCommandResponse response_proto;
    response_proto.set_response(next_response_);
    brillo::dbus_utils::WriteDBusArgs(&writer, response_proto);
    return base::ok(std::move(dbus_response));
  }

  std::string next_response_;
  std::string last_command_;
};

}  // namespace

namespace vtpm {

class VtpmDBusProxyTest : public testing::Test {
 public:
  VtpmDBusProxyTest() {
    proxy_.set_init_timeout(base::TimeDelta());
    proxy_.set_init_attempt_delay(base::TimeDelta());
  }

  void SetUp() override {
    ON_CALL(*bus_, Connect()).WillByDefault(Return(true));
    ON_CALL(*bus_, GetObjectProxy(_, _))
        .WillByDefault(Return(object_proxy_.get()));
    ON_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
        .WillByDefault(Return("test-service-owner"));
  }

  void set_next_response(const std::string& response) {
    object_proxy_->next_response_ = response;
  }
  std::string last_command() const {
    std::string last_command = object_proxy_->last_command_;
    object_proxy_->last_command_.clear();
    return last_command;
  }

 protected:
  scoped_refptr<FakeObjectProxy> object_proxy_ = new FakeObjectProxy();
  scoped_refptr<NiceMock<trunks::MockDBusBus>> bus_ =
      new NiceMock<trunks::MockDBusBus>();
  vtpm::VtpmDBusProxy proxy_{bus_};
};

TEST_F(VtpmDBusProxyTest, InitSuccess) {
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _))
      .WillOnce(Return("test-service-owner"))
      .WillOnce(Return("test-service-owner"));
  // Before initialization IsServiceReady fails without checking.
  EXPECT_FALSE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_FALSE(proxy_.IsServiceReady(true /* force_check */));
  EXPECT_TRUE(proxy_.Init());
  EXPECT_TRUE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_TRUE(proxy_.IsServiceReady(true /* force_check */));
}

TEST_F(VtpmDBusProxyTest, InitFailure) {
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _)).WillRepeatedly(Return(""));
  EXPECT_FALSE(proxy_.Init());
  EXPECT_FALSE(proxy_.IsServiceReady(false /* force_check */));
  EXPECT_FALSE(proxy_.IsServiceReady(true /* force_check */));
}

TEST_F(VtpmDBusProxyTest, SendCommandSuccess) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string tpm_response =
      trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);

  EXPECT_TRUE(proxy_.Init());
  set_next_response(tpm_response);
  auto callback = [](const std::string& response) {
    std::string tpm_response =
        trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);
    EXPECT_EQ(tpm_response, response);
  };
  proxy_.SendCommand(command, base::BindOnce(callback));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandAndWaitSuccess) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string tpm_response =
      trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);

  EXPECT_TRUE(proxy_.Init());
  set_next_response(tpm_response);
  EXPECT_EQ(tpm_response, proxy_.SendCommandAndWait(command));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandFailureInit) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  // If Init() failed, SAPI_RC_NO_CONNECTION should be returned
  // without sending a command.
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _)).WillRepeatedly(Return(""));
  EXPECT_FALSE(proxy_.Init());
  set_next_response("");
  auto callback = [](const std::string& response) {
    EXPECT_EQ(trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION),
              response);
  };
  proxy_.SendCommand(command, base::BindOnce(callback));
  EXPECT_EQ("", last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandAndWaitFailureInit) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string trunks_response =
      trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION);
  // If Init() failed, SAPI_RC_NO_CONNECTION should be returned
  // without sending a command.
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _)).WillRepeatedly(Return(""));
  EXPECT_FALSE(proxy_.Init());
  set_next_response("");
  EXPECT_EQ(trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION),
            proxy_.SendCommandAndWait(command));
  EXPECT_EQ("", last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandFailureNoConnection) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  // If Init() succeeded, but service is later lost, it should return
  // SAPI_RC_NO_CONNECTION in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _)).WillRepeatedly(Return(""));
  set_next_response("");
  auto callback = [](const std::string& response) {
    EXPECT_EQ(trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION),
              response);
  };
  proxy_.SendCommand(command, base::BindOnce(callback));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandAndWaitFailureNoConnection) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string trunks_response =
      trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION);
  // If Init() succeeded, but service is later lost, it should return
  // SAPI_RC_NO_CONNECTION in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  EXPECT_CALL(*bus_, GetServiceOwnerAndBlock(_, _)).WillRepeatedly(Return(""));
  set_next_response("");
  EXPECT_EQ(trunks_response, proxy_.SendCommandAndWait(command));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandFailureNoResponse) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  // If Init() succeeded and the service is ready, it should return
  // an appropriate error code in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  set_next_response("");
  auto callback = [](const std::string& response) {
    EXPECT_EQ(trunks::CreateErrorResponse(trunks::SAPI_RC_NO_RESPONSE_RECEIVED),
              response);
  };
  proxy_.SendCommand(command, base::BindOnce(callback));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandAndWaitFailureNoResponse) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string trunks_response =
      trunks::CreateErrorResponse(trunks::SAPI_RC_MALFORMED_RESPONSE);
  // If Init() succeeded and the service is ready, it should return
  // an appropriate error code in case there was no response.
  EXPECT_TRUE(proxy_.Init());
  set_next_response("");
  EXPECT_EQ(trunks_response, proxy_.SendCommandAndWait(command));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandFailureWrongThread) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string tpm_response =
      trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);
  // Attempting to send from a wrong thread should return TRUNKS_RC_IPC_ERROR
  // without sending the command.
  EXPECT_TRUE(proxy_.Init());
  // xor 1 would change the thread id without overflow.
  base::PlatformThreadId fake_id = proxy_.origin_thread_id_for_testing() ^ 1;
  proxy_.set_origin_thread_id_for_testing(fake_id);
  set_next_response(tpm_response);
  auto callback = [](const std::string& response) {
    EXPECT_EQ(trunks::CreateErrorResponse(trunks::TRUNKS_RC_IPC_ERROR),
              response);
  };
  proxy_.SendCommand(command, base::BindOnce(callback));
  EXPECT_EQ("", last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandAndWaitFailureWrongThread) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_FIRST);
  std::string tpm_response =
      trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);
  std::string trunks_response =
      trunks::CreateErrorResponse(trunks::TRUNKS_RC_IPC_ERROR);
  // Attempting to send from a wrong thread should return TRUNKS_RC_IPC_ERROR
  // without sending the command.
  EXPECT_TRUE(proxy_.Init());
  // xor 1 would change the thread id without overflow.
  base::PlatformThreadId fake_id = proxy_.origin_thread_id_for_testing() ^ 1;
  proxy_.set_origin_thread_id_for_testing(fake_id);
  set_next_response(tpm_response);
  EXPECT_EQ(trunks_response, proxy_.SendCommandAndWait(command));
  EXPECT_EQ("", last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandNotGeneric) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_LAST + 1);
  std::string tpm_response =
      trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);

  EXPECT_TRUE(proxy_.Init());
  set_next_response(tpm_response);
  auto callback = [](const std::string& response) {
    std::string tpm_response =
        trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);
    EXPECT_EQ(tpm_response, response);
  };
  proxy_.SendCommand(command, base::BindOnce(callback));
  EXPECT_EQ(command, last_command());
}

TEST_F(VtpmDBusProxyTest, SendCommandAndWaitNotGeneric) {
  std::string command = trunks::CreateCommand(trunks::TPM_CC_LAST + 1);
  std::string tpm_response =
      trunks::CreateErrorResponse(trunks::TPM_RC_SUCCESS);

  EXPECT_TRUE(proxy_.Init());
  set_next_response(tpm_response);
  EXPECT_EQ(tpm_response, proxy_.SendCommandAndWait(command));
  EXPECT_EQ(command, last_command());
}

}  // namespace vtpm

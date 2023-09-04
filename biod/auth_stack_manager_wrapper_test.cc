// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <biod/auth_stack_manager_wrapper.h>

#include <map>
#include <utility>

#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/mock_exported_object_manager.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/property.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <biod/mock_auth_stack_manager.h>
#include <biod/mock_session_state_manager.h>

namespace biod {
namespace {

using brillo::BlobToString;

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

constexpr char kClientConnectionName[] = ":1.33";

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class AuthStackManagerWrapperTest : public ::testing::Test {
 public:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);
    ON_CALL(*bus_, GetExportedObject)
        .WillByDefault(
            Invoke(this, &AuthStackManagerWrapperTest::GetExportedObject));

    proxy_ =
        new dbus::MockObjectProxy(bus_.get(), dbus::kDBusServiceName,
                                  dbus::ObjectPath(dbus::kDBusServicePath));

    ON_CALL(*bus_, GetObjectProxy(dbus::kDBusServiceName, _))
        .WillByDefault(Return(proxy_.get()));

    EXPECT_CALL(*proxy_, DoConnectToSignal(dbus::kDBusInterface, _, _, _))
        .WillRepeatedly(
            Invoke(this, &AuthStackManagerWrapperTest::ConnectToSignal));

    auto mock_auth_stack_manager = std::make_unique<MockAuthStackManager>();
    bio_manager_ = mock_auth_stack_manager.get();

    EXPECT_CALL(*bio_manager_, SetEnrollScanDoneHandler)
        .WillRepeatedly(SaveArg<0>(&on_enroll_scan_done));
    EXPECT_CALL(*bio_manager_, SetAuthScanDoneHandler)
        .WillRepeatedly(SaveArg<0>(&on_auth_scan_done));
    EXPECT_CALL(*bio_manager_, SetSessionFailedHandler)
        .WillRepeatedly(SaveArg<0>(&on_session_failed));

    EXPECT_CALL(*bio_manager_, GetType)
        .WillRepeatedly(Return(BIOMETRIC_TYPE_UNKNOWN));

    object_manager_ =
        std::make_unique<brillo::dbus_utils::MockExportedObjectManager>(
            bus_, dbus::ObjectPath(kBiodServicePath));
    session_manager_ = std::make_unique<MockSessionStateManager>();

    EXPECT_CALL(*session_manager_, AddObserver).Times(1);

    mock_bio_path_ = dbus::ObjectPath(
        base::StringPrintf("%s/%s", kBiodServicePath, "MockAuthStackManager"));

    auto sequencer =
        base::MakeRefCounted<brillo::dbus_utils::AsyncEventSequencer>();

    wrapper_.emplace(
        std::move(mock_auth_stack_manager), object_manager_.get(),
        session_manager_.get(), mock_bio_path_,
        sequencer->GetHandler("Failed to register AuthStackManager", false));
  }

 protected:
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;
  MockAuthStackManager* bio_manager_;
  std::unique_ptr<brillo::dbus_utils::MockExportedObjectManager>
      object_manager_;
  dbus::ObjectPath mock_bio_path_;
  std::map<std::string, scoped_refptr<dbus::MockExportedObject>>
      exported_objects_;
  std::unique_ptr<MockSessionStateManager> session_manager_;
  std::optional<AuthStackManagerWrapper> wrapper_;
  AuthStackManager::EnrollScanDoneCallback on_enroll_scan_done;
  AuthStackManager::AuthScanDoneCallback on_auth_scan_done;
  AuthStackManager::SessionFailedCallback on_session_failed;

  MOCK_METHOD(void,
              ResponseSender,
              (std::unique_ptr<dbus::Response> response),
              ());
  std::unique_ptr<dbus::Response> CallMethod(dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> StartEnrollSession(
      dbus::ObjectPath* object_path);
  std::unique_ptr<dbus::Response> CreateCredential(
      const CreateCredentialRequestV2& request, CreateCredentialReply* reply);
  std::unique_ptr<dbus::Response> StartAuthSession(
      dbus::ObjectPath* object_path);
  void EmitNameOwnerChangedSignal(const std::string& name,
                                  const std::string& old_owner,
                                  const std::string& new_owner);
  void ExpectStartEnrollSession();
  void ExpectStartAuthSession();

 private:
  std::map<std::string, dbus::ObjectProxy::SignalCallback> signal_callbacks_;
  std::map<std::string, dbus::ExportedObject::MethodCallCallback>
      method_callbacks_;
  base::test::SingleThreadTaskEnvironment task_environment_;

  void ConnectToSignal(
      const std::string& interface_name,
      const std::string& signal_name,
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback* on_connected_callback);
  dbus::ExportedObject* GetExportedObject(const dbus::ObjectPath& object_path);
  void ExportMethod(
      const std::string& interface_name,
      const std::string& method_name,
      const dbus::ExportedObject::MethodCallCallback& method_call_callback,
      dbus::ExportedObject::OnExportedCallback on_exported_callback);
  bool ExportMethodAndBlock(
      const std::string& interface_name,
      const std::string& method_name,
      const dbus::ExportedObject::MethodCallCallback& method_call_callback);
};

void AuthStackManagerWrapperTest::ConnectToSignal(
    const std::string& interface_name,
    const std::string& signal_name,
    dbus::ObjectProxy::SignalCallback signal_callback,
    dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
  EXPECT_EQ(interface_name, dbus::kDBusInterface);
  signal_callbacks_[signal_name] = std::move(signal_callback);
  task_environment_.GetMainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(*on_connected_callback), interface_name,
                     signal_name, true /* success */));
}

void AuthStackManagerWrapperTest::EmitNameOwnerChangedSignal(
    const std::string& name,
    const std::string& old_owner,
    const std::string& new_owner) {
  const auto it = signal_callbacks_.find("NameOwnerChanged");
  ASSERT_TRUE(it != signal_callbacks_.end())
      << "Client didn't register for NameOwnerChanged signal";

  dbus::Signal signal(dbus::kDBusInterface, "NameOwnerChanged");
  dbus::MessageWriter writer(&signal);
  writer.AppendString(name);
  writer.AppendString(old_owner);
  writer.AppendString(new_owner);

  it->second.Run(&signal);
}

dbus::ExportedObject* AuthStackManagerWrapperTest::GetExportedObject(
    const dbus::ObjectPath& object_path) {
  auto iter = exported_objects_.find(object_path.value());
  if (iter != exported_objects_.end()) {
    return iter->second.get();
  }

  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus_.get(), object_path);
  exported_objects_[object_path.value()] = exported_object;

  ON_CALL(*exported_object, ExportMethod)
      .WillByDefault(Invoke(this, &AuthStackManagerWrapperTest::ExportMethod));
  ON_CALL(*exported_object, ExportMethodAndBlock)
      .WillByDefault(
          Invoke(this, &AuthStackManagerWrapperTest::ExportMethodAndBlock));

  return exported_object.get();
}

void AuthStackManagerWrapperTest::ExportMethod(
    const std::string& interface_name,
    const std::string& method_name,
    const dbus::ExportedObject::MethodCallCallback& method_call_callback,
    dbus::ExportedObject::OnExportedCallback on_exported_callback) {
  std::string full_name = interface_name + "." + method_name;
  method_callbacks_[full_name] = method_call_callback;

  task_environment_.GetMainThreadTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(std::move(on_exported_callback), interface_name,
                                method_name, true /* success */));
}

bool AuthStackManagerWrapperTest::ExportMethodAndBlock(
    const std::string& interface_name,
    const std::string& method_name,
    const dbus::ExportedObject::MethodCallCallback& method_call_callback) {
  std::string full_name = interface_name + "." + method_name;
  method_callbacks_[full_name] = method_call_callback;

  return true;
}

std::unique_ptr<dbus::Response> AuthStackManagerWrapperTest::CallMethod(
    dbus::MethodCall* method_call) {
  std::string full_name =
      method_call->GetInterface() + "." + method_call->GetMember();

  std::unique_ptr<dbus::Response> response;
  EXPECT_CALL(*this, ResponseSender)
      .WillOnce([&response](std::unique_ptr<dbus::Response> result) {
        response = std::move(result);
      });

  auto response_sender = base::BindOnce(
      &AuthStackManagerWrapperTest::ResponseSender, base::Unretained(this));
  method_call->SetSerial(1);
  method_call->SetSender(kClientConnectionName);

  auto iter = method_callbacks_.find(full_name);
  EXPECT_TRUE(iter != method_callbacks_.end());
  dbus::ExportedObject::MethodCallCallback& method_callback = iter->second;
  method_callback.Run(method_call, std::move(response_sender));

  return response;
}

std::unique_ptr<dbus::Response> AuthStackManagerWrapperTest::StartEnrollSession(
    dbus::ObjectPath* object_path) {
  StartEnrollSessionRequest request;
  dbus::MethodCall start_enroll_session(
      kAuthStackManagerInterface, kAuthStackManagerStartEnrollSessionMethod);
  dbus::MessageWriter writer(&start_enroll_session);
  writer.AppendProtoAsArrayOfBytes(request);

  auto response = CallMethod(&start_enroll_session);
  if (response->GetMessageType() == dbus::Message::MESSAGE_METHOD_RETURN) {
    dbus::MessageReader reader(response.get());
    reader.PopObjectPath(object_path);
  }

  return response;
}

std::unique_ptr<dbus::Response> AuthStackManagerWrapperTest::CreateCredential(
    const CreateCredentialRequestV2& request, CreateCredentialReply* reply) {
  dbus::MethodCall create_credential(kAuthStackManagerInterface,
                                     kAuthStackManagerCreateCredentialMethod);
  dbus::MessageWriter writer(&create_credential);
  writer.AppendProtoAsArrayOfBytes(request);

  auto response = CallMethod(&create_credential);
  if (response->GetMessageType() == dbus::Message::MESSAGE_METHOD_RETURN) {
    dbus::MessageReader reader(response.get());
    reader.PopArrayOfBytesAsProto(reply);
  }

  return response;
}

std::unique_ptr<dbus::Response> AuthStackManagerWrapperTest::StartAuthSession(
    dbus::ObjectPath* object_path) {
  StartAuthSessionRequest request;
  dbus::MethodCall start_auth_session(kAuthStackManagerInterface,
                                      kAuthStackManagerStartAuthSessionMethod);
  dbus::MessageWriter writer(&start_auth_session);
  writer.AppendProtoAsArrayOfBytes(request);

  auto response = CallMethod(&start_auth_session);
  if (response->GetMessageType() == dbus::Message::MESSAGE_METHOD_RETURN) {
    dbus::MessageReader reader(response.get());
    reader.PopObjectPath(object_path);
  }

  return response;
}

void AuthStackManagerWrapperTest::ExpectStartEnrollSession() {
  auto enroll_session = AuthStackManager::Session(
      base::BindOnce(&MockAuthStackManager::EndEnrollSession,
                     bio_manager_->session_weak_factory_.GetWeakPtr()));
  EXPECT_CALL(*bio_manager_, StartEnrollSession)
      .WillOnce(Return(ByMove(std::move(enroll_session))));
}

void AuthStackManagerWrapperTest::ExpectStartAuthSession() {
  auto auth_session = AuthStackManager::Session(
      base::BindOnce(&MockAuthStackManager::EndAuthSession,
                     bio_manager_->session_weak_factory_.GetWeakPtr()));
  EXPECT_CALL(*bio_manager_, StartAuthSession)
      .WillOnce(Return(ByMove(std::move(auth_session))));
}

TEST_F(AuthStackManagerWrapperTest, TestStartEnrollSession) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);
  dbus::ObjectPath expected_object_path(mock_bio_path_.value() +
                                        "/EnrollSession");
  EXPECT_EQ(object_path, expected_object_path);

  // Expect that enroll session will be finished on destruction of
  // the enroll_session object. EXPECT_CALL is able to catch calls from
  // enroll_session destructor which is called at the end of this
  // test.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(1);
}

TEST_F(AuthStackManagerWrapperTest, TestStartEnrollSessionThenCancel) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Cancel enroll session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(1);
  dbus::MethodCall cancel_enroll_session(kEnrollSessionInterface,
                                         kEnrollSessionCancelMethod);
  auto cancel_response = CallMethod(&cancel_enroll_session);

  // Make sure enroll session is not killed on destruction
  // of the enroll_session object. EXPECT_CALL is able to catch calls from
  // enroll_session destructor which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(0);
}

TEST_F(AuthStackManagerWrapperTest, TestEnrollSessionFinish) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Expect enroll session is active when not finished.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(0);

  AuthStackManager::EnrollStatus enroll_status = {false, 50};
  on_enroll_scan_done.Run(ScanResult::SCAN_RESULT_SUCCESS, enroll_status);

  // Finish enroll session.
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(1);

  AuthStackManager::EnrollStatus enroll_status_finish = {true, 100};
  on_enroll_scan_done.Run(ScanResult::SCAN_RESULT_SUCCESS,
                          enroll_status_finish);
}

TEST_F(AuthStackManagerWrapperTest, TestEnrollSessionSignal) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Expect enroll scan done signal is emitted when enroll session not finished.
  auto iter = exported_objects_.find(mock_bio_path_.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, SendSignal).WillOnce([](dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), kAuthStackManagerInterface);
    EXPECT_EQ(signal->GetMember(), kBiometricsManagerEnrollScanDoneSignal);
    dbus::MessageReader reader(signal);
    EnrollScanDone proto;
    reader.PopArrayOfBytesAsProto(&proto);
    EXPECT_FALSE(proto.done());
    EXPECT_EQ(proto.scan_result(), ScanResult::SCAN_RESULT_SUCCESS);
    EXPECT_EQ(proto.percent_complete(), 50);
    EXPECT_TRUE(proto.auth_nonce().empty());
  });

  AuthStackManager::EnrollStatus enroll_status = {false, 50};
  on_enroll_scan_done.Run(ScanResult::SCAN_RESULT_SUCCESS, enroll_status);

  // Finish enroll session.
  EXPECT_CALL(*object, SendSignal).WillOnce([](dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), kAuthStackManagerInterface);
    EXPECT_EQ(signal->GetMember(), kBiometricsManagerEnrollScanDoneSignal);
    dbus::MessageReader reader(signal);
    EnrollScanDone proto;
    reader.PopArrayOfBytesAsProto(&proto);
    EXPECT_TRUE(proto.done());
    EXPECT_EQ(proto.scan_result(), ScanResult::SCAN_RESULT_SUCCESS);
    EXPECT_EQ(proto.percent_complete(), 100);
  });

  AuthStackManager::EnrollStatus enroll_status_finish = {true, 100};
  on_enroll_scan_done.Run(ScanResult::SCAN_RESULT_SUCCESS,
                          enroll_status_finish);
}

TEST_F(AuthStackManagerWrapperTest, TestOnEnrollScanDoneWithoutActiveSession) {
  auto iter = exported_objects_.find(mock_bio_path_.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  // Expect that no signal will be sent when there is no enroll session.
  EXPECT_CALL(*object, SendSignal).Times(0);

  AuthStackManager::EnrollStatus enroll_status_finish = {true, 100};
  on_enroll_scan_done.Run(ScanResult::SCAN_RESULT_SUCCESS,
                          enroll_status_finish);
}

TEST_F(AuthStackManagerWrapperTest, TestStartEnrollSessionFailed) {
  dbus::ObjectPath object_path;
  // Empty enroll session indicates that we were not able to start
  // enroll session.
  EXPECT_CALL(*bio_manager_, StartEnrollSession)
      .WillOnce(Return(ByMove(AuthStackManager::Session())));

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() == dbus::Message::MESSAGE_ERROR);
}

TEST_F(AuthStackManagerWrapperTest, TestOnSessionFailedEndsEnrollSession) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Fail enroll session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(1);
  on_session_failed.Run();

  // Make sure enroll session is not killed on destruction
  // of the enroll_session object. EXPECT_CALL is able to catch calls from
  // enroll_session destructor which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(0);
}

TEST_F(AuthStackManagerWrapperTest, TestEnrollOnSessionFailedSendSignal) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  auto iter = exported_objects_.find(mock_bio_path_.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, SendSignal).WillOnce([](dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), kAuthStackManagerInterface);
    EXPECT_EQ(signal->GetMember(), kBiometricsManagerSessionFailedSignal);
  });

  // Fail enroll session.
  on_session_failed.Run();
}

TEST_F(AuthStackManagerWrapperTest, TestEnrollSessionOwnerDies) {
  dbus::ObjectPath object_path;
  ExpectStartEnrollSession();

  auto response = StartEnrollSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Fail enroll session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(1);
  EmitNameOwnerChangedSignal(kClientConnectionName, kClientConnectionName, "");

  // Make sure enroll session is not killed on destruction
  // of the enroll_session object. EXPECT_CALL is able to catch calls from
  // enroll_session destructor which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndEnrollSession).Times(0);
}

TEST_F(AuthStackManagerWrapperTest, TestStartAuthSession) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);
  dbus::ObjectPath expected_object_path(mock_bio_path_.value() +
                                        "/AuthSession");
  EXPECT_EQ(object_path, expected_object_path);

  // Expect that auth session will be killed on destruction of the auth_sesson
  // object. EXPECT_CALL is able to catch calls from auth_session destructor
  // which is called at the end of this test.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(1);
}

TEST_F(AuthStackManagerWrapperTest, TestStartAuthSessionThenEnd) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Cancel auth session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(1);
  dbus::MethodCall end_auth_session(kAuthSessionInterface,
                                    kAuthSessionEndMethod);
  auto cancel_response = CallMethod(&end_auth_session);

  // Make sure auth session is not killed on destruction of the auth_session
  // object. EXPECT_CALL is able to catch calls from auth_session destructor
  // which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(0);
}

TEST_F(AuthStackManagerWrapperTest, TestStartAuthSessionSuccess) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Expect that calling OnAuthScanDone doesn't end auth session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(0);

  on_auth_scan_done.Run();

  // Expect that auth session will be killed on destruction of the auth_session
  // object. EXPECT_CALL is able to catch calls from auth_session destructor
  // which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(1);
}

TEST_F(AuthStackManagerWrapperTest, TestStartAuthSessionSuccessSignal) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Check signal contents.
  auto iter = exported_objects_.find(mock_bio_path_.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, SendSignal).WillOnce([](dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), kAuthStackManagerInterface);
    EXPECT_EQ(signal->GetMember(), kBiometricsManagerAuthScanDoneSignal);
  });

  on_auth_scan_done.Run();
}

TEST_F(AuthStackManagerWrapperTest, TestStartAuthSessionFailed) {
  dbus::ObjectPath object_path;
  // Empty auth session indicates that we were not able to start
  // enroll session.
  EXPECT_CALL(*bio_manager_, StartAuthSession)
      .WillOnce(Return(ByMove(AuthStackManager::Session())));

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() == dbus::Message::MESSAGE_ERROR);
}

TEST_F(AuthStackManagerWrapperTest, TestOnSessionFailedEndsAuthSession) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Cancel auth session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(1);
  on_session_failed.Run();

  // Make sure auth session is not killed on destruction of the auth_session
  // object. EXPECT_CALL is able to catch calls from auth_session destructor
  // which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(0);
}

TEST_F(AuthStackManagerWrapperTest, TestAuthOnSessionFailedSendsSignal) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  auto iter = exported_objects_.find(mock_bio_path_.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, SendSignal).WillOnce([](dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), kAuthStackManagerInterface);
    EXPECT_EQ(signal->GetMember(), kBiometricsManagerSessionFailedSignal);
  });

  // Fail enroll session.
  on_session_failed.Run();
}

TEST_F(AuthStackManagerWrapperTest, TestAuthSessionOwnerDies) {
  dbus::ObjectPath object_path;
  ExpectStartAuthSession();

  auto response = StartAuthSession(&object_path);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);

  // Cancel auth session.
  auto iter = exported_objects_.find(object_path.value());
  ASSERT_TRUE(iter != exported_objects_.end());
  scoped_refptr<dbus::MockExportedObject> object = iter->second;
  EXPECT_CALL(*object, Unregister).Times(1);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(1);
  EmitNameOwnerChangedSignal(kClientConnectionName, kClientConnectionName, "");

  // Make sure auth session is not killed on destruction of the auth_session
  // object. EXPECT_CALL is able to catch calls from auth_session destructor
  // which is called at the end of this test.
  EXPECT_CALL(*object, Unregister).Times(0);
  EXPECT_CALL(*bio_manager_, EndAuthSession).Times(0);
}

TEST_F(AuthStackManagerWrapperTest, TestCreateCredential) {
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const std::string kRecordId("record_id");

  CreateCredentialRequestV2 request;
  request.mutable_pub()->set_x(BlobToString(kPubInX));
  request.mutable_pub()->set_y(BlobToString(kPubInY));

  CreateCredentialReply reply;
  reply.set_status(CreateCredentialReply::SUCCESS);
  reply.set_encrypted_secret(BlobToString(kEncryptedSecret));
  reply.mutable_pub()->set_x(BlobToString(kPubOutX));
  reply.mutable_pub()->set_y(BlobToString(kPubOutY));
  reply.set_record_id(kRecordId);

  EXPECT_CALL(*bio_manager_, CreateCredential(EqualsProto(request)))
      .WillOnce(Return(reply));

  CreateCredentialReply returned_reply;
  auto response = CreateCredential(request, &returned_reply);
  EXPECT_TRUE(response->GetMessageType() ==
              dbus::Message::MESSAGE_METHOD_RETURN);
  EXPECT_THAT(returned_reply, EqualsProto(reply));
}

TEST_F(AuthStackManagerWrapperTest, TestOnUserLoggedInLoggedOut) {
  const std::string kUserId("testuser");

  EXPECT_CALL(*bio_manager_, OnUserLoggedIn(kUserId));
  EXPECT_CALL(*bio_manager_, OnUserLoggedOut);

  wrapper_->OnUserLoggedIn(kUserId, true);
  wrapper_->OnUserLoggedOut();
}

}  // namespace
}  // namespace biod

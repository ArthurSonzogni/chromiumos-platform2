// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/device_user.h"

#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "cryptohome/proto_bindings/UserDataAuth.pb.h"
#include "dbus/cryptohome/dbus-constants.h"
#include "dbus/login_manager/dbus-constants.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "session_manager-client-test/session_manager/dbus-proxy-mocks.h"
#include "user_data_auth/dbus-proxy-mocks.h"

namespace secagentd::testing {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

constexpr char kDeviceUser[] = "deviceUser@email.com";
constexpr char kSanitized[] = "943cebc444e3e19da9a2dbf9c8a473bc7cc16d9d";
constexpr char kAffiliationID[] = "C02gxaaci";

class DeviceUserTestFixture : public ::testing::Test {
 protected:
  DeviceUserTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  void SetUp() override {
    session_manager_ =
        std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
    session_manager_ref_ = session_manager_.get();

    // Setup root directory.
    ASSERT_TRUE(fake_root_.CreateUniqueTempDir());
    secagentd_directory_ = fake_root_.GetPath().Append(kSecagentdDirectory);
    ASSERT_TRUE(base::CreateDirectory(secagentd_directory_));

    // Setup mock cryptohome.
    cryptohome_proxy_ =
        std::make_unique<org::chromium::UserDataAuthInterfaceProxyMock>();
    cryptohome_object_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), user_data_auth::kUserDataAuthServiceName,
        dbus::ObjectPath(user_data_auth::kUserDataAuthServicePath));
    EXPECT_CALL(*cryptohome_proxy_, GetObjectProxy)
        .WillRepeatedly(Return(cryptohome_object_proxy_.get()));
    EXPECT_CALL(*cryptohome_object_proxy_, DoWaitForServiceToBeAvailable(_))
        .WillRepeatedly(
            WithArg<0>(Invoke([](base::OnceCallback<void(bool)>* cb) mutable {
              std::move(*cb).Run(true);
            })));
    EXPECT_CALL(*cryptohome_proxy_, DoRegisterRemoveCompletedSignalHandler)
        .WillOnce(WithArgs<0, 1>(Invoke(
            [this](
                base::RepeatingCallback<void(
                    const user_data_auth::RemoveCompleted&)> cb,
                base::OnceCallback<void(const std::string&, const std::string&,
                                        bool)>* registration_result_cb) {
              remove_completed_cb_ = cb;
              std::move(*registration_result_cb)
                  .Run("org.chromium.UserDataAuthInterface", "RemoveCompleted",
                       true);
            })));

    device_user_ = DeviceUser::CreateForTesting(std::move(session_manager_),
                                                std::move(cryptohome_proxy_),
                                                fake_root_.GetPath());
    device_user_->RegisterRemoveCompletedHandler();
  }

  std::string GetUser() { return device_user_->device_user_; }

  void ChangeSessionState(const std::string& state) {
    device_user_->OnSessionStateChange(state);
  }

  std::vector<uint8_t> CreateExpectedDescriptorBlob(const std::string& type,
                                                    const std::string& user) {
    login_manager::PolicyDescriptor descriptor;
    descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
    descriptor.set_account_id(user);

    if (type == "user") {
      descriptor.set_account_type(
          login_manager::PolicyAccountType::ACCOUNT_TYPE_USER);
    } else if (type == "device") {
      descriptor.set_account_type(
          login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE);
    } else {
      CHECK(false);
    }

    std::string descriptor_string = descriptor.SerializeAsString();
    return std::vector<uint8_t>(descriptor_string.begin(),
                                descriptor_string.end());
  }

  std::vector<uint8_t> CreatePolicyFetchResponseBlob(
      const std::string& type, const std::string& affiliation_id) {
    // Add ID based on policy type.
    enterprise_management::PolicyData policy_data;
    if (type == "user") {
      auto id = policy_data.add_user_affiliation_ids();
      *id = affiliation_id;
    } else if (type == "device") {
      auto id = policy_data.add_device_affiliation_ids();
      *id = affiliation_id;
    } else {
      CHECK(false);
    }

    enterprise_management::PolicyFetchResponse response;
    CHECK(policy_data.SerializeToString(response.mutable_policy_data()));
    auto serialized = response.SerializeAsString();
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
  }

  void SaveRegistrationCallbacks() {
    session_manager_object_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), login_manager::kSessionManagerServiceName,
        dbus::ObjectPath(login_manager::kSessionManagerServicePath));
    EXPECT_CALL(*session_manager_ref_, GetObjectProxy)
        .Times(2)
        .WillRepeatedly(Return(session_manager_object_proxy_.get()));
    EXPECT_CALL(*session_manager_object_proxy_, DoWaitForServiceToBeAvailable)
        .WillOnce(
            WithArg<0>(Invoke([](base::OnceCallback<void(bool)>* cb) mutable {
              std::move(*cb).Run(true);
            })));
    EXPECT_CALL(*session_manager_object_proxy_, SetNameOwnerChangedCallback)
        .WillOnce(WithArg<0>(
            Invoke([this](const base::RepeatingCallback<void(
                              const std::string&, const std::string&)>& cb) {
              name_change_cb_ = cb;
            })));
    EXPECT_CALL(*session_manager_ref_,
                DoRegisterSessionStateChangedSignalHandler)
        .WillOnce(WithArgs<0, 1>(Invoke(
            [this](
                const base::RepeatingCallback<void(const std::string&)>&
                    registration_cb,
                base::OnceCallback<void(const std::string&, const std::string&,
                                        bool)>* registration_result_cb) {
              registration_cb_ = registration_cb;
              registration_result_cb_ = std::move(*registration_result_cb);
            })));
  }

  void SetDeviceUser(const std::string& user) {
    device_user_->device_user_ = user;
  }

  bool GetDeviceUserReady() { return device_user_->device_user_ready_; }

  void SetDeviceUserReady(bool ready) {
    device_user_->device_user_ready_ = ready;
  }

  size_t GetDeviceUserReadyCbsSize() {
    return device_user_->on_device_user_ready_cbs_.size();
  }

  void PushBackDeviceUserReadyCbs() {
    device_user_->on_device_user_ready_cbs_.push_back(base::DoNothing());
  }

  base::test::TaskEnvironment task_environment_;
  base::FilePath secagentd_directory_;
  base::ScopedTempDir fake_root_;
  base::OnceCallback<void(const std::string&, const std::string&, bool)>
      registration_result_cb_;
  base::RepeatingCallback<void(const std::string&)> registration_cb_;
  base::RepeatingCallback<void(const std::string&, const std::string&)>
      name_change_cb_;
  base::RepeatingCallback<void(const user_data_auth::RemoveCompleted&)>
      remove_completed_cb_;
  scoped_refptr<DeviceUser> device_user_;
  scoped_refptr<dbus::MockObjectProxy> session_manager_object_proxy_;
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> cryptohome_object_proxy_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyMock>
      cryptohome_proxy_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyMock>
      session_manager_;
  org::chromium::SessionManagerInterfaceProxyMock* session_manager_ref_;
};

TEST_F(DeviceUserTestFixture, TestAffiliatedUser) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(kDeviceUser, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestUserAlreadySignedIn) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
        return true;
      })));

  EXPECT_FALSE(GetDeviceUserReady());
  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  std::move(registration_result_cb_)
      .Run("org.chromium.SessionManagerInterface", "SessionStateChanged", true);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);
  EXPECT_TRUE(GetDeviceUserReady());

  EXPECT_EQ(kDeviceUser, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestStoredUserAffiliated) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillRepeatedly(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUser;
            *sanitized = kSanitized;
            return true;
          })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .Times(1)
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(kDeviceUser, GetUser());
  base::FilePath affiliated_file =
      secagentd_directory_.Append(kSanitized).Append("affiliated");
  ASSERT_TRUE(base::PathExists(affiliated_file));

  // Trigger callback again to verify the file is read from.
  SetDeviceUser(device_user::kEmpty);
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);
  EXPECT_EQ(kDeviceUser, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestStoredUserUnaffiliated) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillRepeatedly(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUser;
            *sanitized = kSanitized;
            return true;
          })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .Times(1)
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", "DifferentID");
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  // Just verify that the username is a valid uuid because it
  // is random each time.
  EXPECT_TRUE(GetUser().starts_with(device_user::kUnaffiliatedPrefix) &&
              base::Uuid::ParseCaseInsensitive(
                  GetUser().substr(strlen(device_user::kUnaffiliatedPrefix)))
                  .is_valid());
  base::FilePath unaffiliated_file =
      secagentd_directory_.Append(kSanitized).Append("unaffiliated");
  ASSERT_TRUE(base::PathExists(unaffiliated_file));
  std::string username;
  ASSERT_TRUE(base::ReadFileToString(unaffiliated_file, &username));
  EXPECT_TRUE(username.starts_with(device_user::kUnaffiliatedPrefix) &&
              base::Uuid::ParseCaseInsensitive(
                  username.substr(strlen(device_user::kUnaffiliatedPrefix)))
                  .is_valid());

  // Trigger callback again to verify the file is read from.
  SetDeviceUser(device_user::kEmpty);
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);
  EXPECT_TRUE(GetUser().starts_with(device_user::kUnaffiliatedPrefix) &&
              base::Uuid::ParseCaseInsensitive(
                  GetUser().substr(strlen(device_user::kUnaffiliatedPrefix)))
                  .is_valid());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestLogout) {
  SetDeviceUser(kDeviceUser);
  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStopping);
  EXPECT_EQ(device_user::kEmpty, GetUser());

  SetDeviceUser(kDeviceUser);
  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStopped);
  EXPECT_EQ(device_user::kEmpty, GetUser());
}

TEST_F(DeviceUserTestFixture, TestUnaffiliatedUser) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", "DifferentID");
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_TRUE(GetUser().starts_with(device_user::kUnaffiliatedPrefix) &&
              base::Uuid::ParseCaseInsensitive(
                  GetUser().substr(strlen(device_user::kUnaffiliatedPrefix)))
                  .is_valid());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestGuestUser) {
  EXPECT_CALL(*session_manager_ref_, RetrievePolicyEx)
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = true;
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(device_user::kGuest, GetUser());
  ASSERT_EQ(0, device_user_->GetUsernamesForRedaction().size());
}

TEST_F(DeviceUserTestFixture, TestFailedRegistration) {
  session_manager_object_proxy_ = new dbus::MockObjectProxy(
      bus_.get(), login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));
  EXPECT_CALL(*session_manager_ref_, GetObjectProxy)
      .Times(2)
      .WillRepeatedly(Return(session_manager_object_proxy_.get()));
  EXPECT_CALL(*session_manager_object_proxy_, DoWaitForServiceToBeAvailable)
      .WillRepeatedly(
          WithArg<0>(Invoke([](base::OnceCallback<void(bool)>* cb) mutable {
            std::move(*cb).Run(true);
          })));
  EXPECT_CALL(*session_manager_ref_, DoRegisterSessionStateChangedSignalHandler)
      .WillOnce(WithArg<1>(
          Invoke([](base::OnceCallback<void(const std::string&,
                                            const std::string&, bool)>* cb) {
            std::move(*cb).Run("dbus", "register", false);
          })));

  device_user_->RegisterSessionChangeHandler();

  EXPECT_EQ(device_user::kUnknown, GetUser());
}

TEST_F(DeviceUserTestFixture, TestFailedGuestSessionRetrieval) {
  EXPECT_CALL(*session_manager_ref_, RetrievePolicyEx)
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<1>(Invoke([](brillo::ErrorPtr* error) {
        *error = brillo::Error::Create(FROM_HERE, "", "",
                                       "IsGuestSessionActive failed");
        return false;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = device_user::kEmpty;
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(device_user::kUnknown, GetUser());
}

TEST_F(DeviceUserTestFixture, TestFailedPrimarySessionRetrieval) {
  EXPECT_CALL(*session_manager_ref_, RetrievePolicyEx)
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
        *error =
            brillo::Error::Create(FROM_HERE, "", "", "RetrievePolicyEx failed");
        return false;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(device_user::kUnknown, GetUser());
}

TEST_F(DeviceUserTestFixture, TestFailedRetrievePolicyEx) {
  EXPECT_CALL(*session_manager_ref_, RetrievePolicyEx)
      .Times(2)
      .WillRepeatedly(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
        *error =
            brillo::Error::Create(FROM_HERE, "", "", "RetrievePolicyEx failed");
        return false;
      })));
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(device_user::kUnknown, GetUser());
}

TEST_F(DeviceUserTestFixture, TestFailedParsingResponse) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .WillOnce(WithArg<1>(Invoke([](std::vector<uint8_t>* out_blob) {
        std::vector<uint8_t> bad_blob;
        bad_blob.push_back(1);
        *out_blob = bad_blob;
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(device_user::kUnknown, GetUser());
}

TEST_F(DeviceUserTestFixture, TestFailedParsingPolicy) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .WillOnce(WithArg<1>(Invoke([](std::vector<uint8_t>* out_blob) {
        std::vector<uint8_t> bad_blob;
        bad_blob.push_back(1);
        *out_blob = bad_blob;
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(device_user::kUnknown, GetUser());
}

TEST_F(DeviceUserTestFixture, TestSessionManagerCrash) {
  SetDeviceUser(kDeviceUser);

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  // Simulate "crash" by invoking name change method.
  name_change_cb_.Run("old_name", "");
  name_change_cb_.Run("", "new_name");

  EXPECT_EQ(device_user::kEmpty, GetUser());
}

TEST_F(DeviceUserTestFixture, TestLoginLogoutMultipleTimesForRedaction) {
  int times = 3;
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .Times(times)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));

  std::vector<std::string> device_users;
  for (int i = 0; i < times; i++) {
    auto device_user = absl::StrFormat("user%d@email.com", i);
    auto sanitized_name = absl::StrFormat("sanitized%d", i);

    device_users.push_back(device_user);

    EXPECT_CALL(*session_manager_ref_,
                RetrievePolicyEx(
                    CreateExpectedDescriptorBlob("user", device_user), _, _, _))
        .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
          *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
          return true;
        })));
  }

  // All expects must be nested because the parameters are the same each time.
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = "user0@email.com";
            *sanitized = "sanitized0";
            return true;
          })))
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = "user1@email.com";
            *sanitized = "sanitized1";
            return true;
          })))
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = "user2@email.com";
            *sanitized = "sanitized2";
            return true;
          })));

  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();

  for (int i = 0; i < times; i++) {
    registration_cb_.Run(kStarted);
    task_environment_.FastForwardBy(kDelayForFirstUserInit);

    EXPECT_EQ(device_users[i], GetUser());
    ASSERT_EQ(i + 1, device_user_->GetUsernamesForRedaction().size());
    EXPECT_EQ(device_users[i],
              device_user_->GetUsernamesForRedaction().front());

    registration_cb_.Run(kStopped);
    EXPECT_EQ(device_user::kEmpty, GetUser());
    ASSERT_EQ(i + 1, device_user_->GetUsernamesForRedaction().size());
    EXPECT_EQ(device_users[i],
              device_user_->GetUsernamesForRedaction().front());
  }
}

TEST_F(DeviceUserTestFixture, TestLoginLogoutSameUsernameAffiliated) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .Times(2)
      .WillRepeatedly(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUser;
            *sanitized = kSanitized;
            return true;
          })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .Times(1)
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  EXPECT_EQ(kDeviceUser, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());

  registration_cb_.Run(kStopped);
  EXPECT_EQ(device_user::kEmpty, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());

  registration_cb_.Run(kStarted);
  EXPECT_EQ(kDeviceUser, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());

  registration_cb_.Run(kStopped);
  EXPECT_EQ(device_user::kEmpty, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestLoginLogoutSameUsernameUnaffiliated) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .Times(2)
      .WillRepeatedly(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUser;
            *sanitized = kSanitized;
            return true;
          })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .Times(1)
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", "differentID");
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);

  std::string unaffiliated_user = GetUser();
  EXPECT_TRUE(
      unaffiliated_user.starts_with(device_user::kUnaffiliatedPrefix) &&
      base::Uuid::ParseCaseInsensitive(
          unaffiliated_user.substr(strlen(device_user::kUnaffiliatedPrefix)))
          .is_valid());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());

  registration_cb_.Run(kStopped);
  EXPECT_EQ(device_user::kEmpty, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());

  registration_cb_.Run(kStarted);
  EXPECT_EQ(unaffiliated_user, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());

  registration_cb_.Run(kStopped);
  EXPECT_EQ(device_user::kEmpty, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
}

TEST_F(DeviceUserTestFixture, TestLocalAccount) {
  const std::unordered_map<std::string, std::string> local_account_map = {
      {"6b696f736b5f617070@public-accounts.device-local.localhost",
       device_user::kManagedGuest},
      {"6b696f736b5f617070@web-kiosk-apps.device-local.localhost",
       device_user::kKioskApp},
      {"6b696f736b5f617070@saml-public-accounts.device-local.localhost",
       device_user::kSAML},
      {"6b696f736b5f617070@web-kiosk-apps.device-local.localhost",
       device_user::kWebKioskApp}};

  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();

  for (auto const& [key, val] : local_account_map) {
    EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
        .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
          *is_guest = false;
          return true;
        })));
    EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
        .WillOnce(WithArg<0>(Invoke([&key](std::string* username) {
          *username = key;
          return true;
        })));

    registration_cb_.Run(kStarted);
    task_environment_.FastForwardBy(kDelayForFirstUserInit);

    EXPECT_EQ(val, GetUser());
  }
}

TEST_F(DeviceUserTestFixture, TestGetDeviceUserAsync) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillOnce(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArg<0>(Invoke([](std::string* username) {
        *username = kDeviceUser;
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  registration_cb_.Run(kStarted);
  EXPECT_FALSE(GetDeviceUserReady());
  base::test::TestFuture<const std::string&> future_not_ready_1;
  device_user_->GetDeviceUserAsync(future_not_ready_1.GetCallback());

  base::test::TestFuture<const std::string&> future_not_ready_2;
  device_user_->GetDeviceUserAsync(future_not_ready_2.GetCallback());
  EXPECT_FALSE(future_not_ready_1.IsReady());
  EXPECT_EQ(2, GetDeviceUserReadyCbsSize());
  task_environment_.FastForwardBy(kDelayForFirstUserInit);
  EXPECT_TRUE(future_not_ready_1.Wait());
  EXPECT_TRUE(future_not_ready_2.Wait());
  EXPECT_TRUE(GetDeviceUserReady());
  base::test::TestFuture<const std::string&> future_ready;
  device_user_->GetDeviceUserAsync(future_ready.GetCallback());
  EXPECT_TRUE(future_ready.Wait());

  EXPECT_EQ(kDeviceUser, GetUser());
  ASSERT_EQ(1, device_user_->GetUsernamesForRedaction().size());
  EXPECT_EQ(kDeviceUser, device_user_->GetUsernamesForRedaction().front());
  EXPECT_EQ(0, GetDeviceUserReadyCbsSize());
}

TEST_F(DeviceUserTestFixture, TestClearDeviceUserReadyCbs) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArgs<0>(Invoke([](std::string* username) {
        *username = "6b696f736b5f617070@public-accounts.device-local.localhost";
        return true;
      })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  EXPECT_EQ(0, GetDeviceUserReadyCbsSize());
  PushBackDeviceUserReadyCbs();
  EXPECT_EQ(1, GetDeviceUserReadyCbsSize());

  registration_cb_.Run(kStarted);

  EXPECT_TRUE(GetDeviceUserReady());
  EXPECT_EQ(0, GetDeviceUserReadyCbsSize());
}

TEST_F(DeviceUserTestFixture, TestGetUsernameBasedOnAffiliation) {
  EXPECT_CALL(*session_manager_ref_, IsGuestSessionActive)
      .Times(2)
      .WillRepeatedly(WithArg<0>(Invoke([](bool* is_guest) {
        *is_guest = false;
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_, RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUser;
            *sanitized = kSanitized;
            return true;
          })))
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceUser;
            *sanitized = "different_sanitized";
            return true;
          })));
  EXPECT_CALL(
      *session_manager_ref_,
      RetrievePolicyEx(CreateExpectedDescriptorBlob("device", ""), _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("device", kAffiliationID);
        return true;
      })));
  EXPECT_CALL(*session_manager_ref_,
              RetrievePolicyEx(
                  CreateExpectedDescriptorBlob("user", kDeviceUser), _, _, _))
      .Times(2)
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", kAffiliationID);
        return true;
      })))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob("user", "DifferentID");
        return true;
      })));

  SaveRegistrationCallbacks();
  device_user_->RegisterSessionChangeHandler();
  // Login and logout affililiated.
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);
  registration_cb_.Run(kStopped);
  base::FilePath affiliated_file =
      secagentd_directory_.Append(kSanitized).Append("affiliated");
  ASSERT_TRUE(base::PathExists(affiliated_file));
  int64_t file_size;
  EXPECT_TRUE(base::GetFileSize(affiliated_file, &file_size));
  EXPECT_EQ(0, file_size);
  std::string username =
      device_user_->GetUsernameBasedOnAffiliation(kDeviceUser, kSanitized);
  EXPECT_EQ(kDeviceUser, username);

  // Login and logout unaffililiated.
  registration_cb_.Run(kStarted);
  task_environment_.FastForwardBy(kDelayForFirstUserInit);
  registration_cb_.Run(kStopped);
  base::FilePath unaffiliated_file =
      secagentd_directory_.Append("different_sanitized").Append("unaffiliated");
  ASSERT_TRUE(base::PathExists(unaffiliated_file));
  username = device_user_->GetUsernameBasedOnAffiliation(kDeviceUser,
                                                         "different_sanitized");
  EXPECT_TRUE(username.starts_with(device_user::kUnaffiliatedPrefix) &&
              base::Uuid::ParseCaseInsensitive(
                  username.substr(strlen(device_user::kUnaffiliatedPrefix)))
                  .is_valid());
}

TEST_F(DeviceUserTestFixture, TestRemoveAffiliationStatus) {
  base::FilePath user_directory = secagentd_directory_.Append(kSanitized);
  ASSERT_TRUE(base::CreateDirectory(user_directory));
  ASSERT_TRUE(base::WriteFile(user_directory.Append("affiliated"), ""));
  EXPECT_TRUE(base::PathExists(user_directory.Append("affiliated")));
  EXPECT_TRUE(base::DirectoryExists(user_directory));

  user_data_auth::RemoveCompleted remove_completed;
  remove_completed.set_sanitized_username(kSanitized);
  remove_completed_cb_.Run(remove_completed);

  EXPECT_FALSE(base::PathExists(user_directory.Append("affiliated")));
  EXPECT_FALSE(base::DirectoryExists(user_directory));
}

}  // namespace secagentd::testing

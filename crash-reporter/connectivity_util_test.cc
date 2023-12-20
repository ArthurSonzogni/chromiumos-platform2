// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/connectivity_util.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <bindings/cloud_policy.pb.h>
#include <bindings/device_management_backend.pb.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <policy/device_policy.h>
#include <session_manager-client-test/session_manager/dbus-proxy-mocks.h>

namespace connectivity_util {

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;
using ::testing::WithArgs;

constexpr char kDeviceGoogleUser[] = "testuser@google.com";
constexpr char kDeviceUserNotAllowed[] = "disallowed_user@gmail.com";
constexpr char kDeviceUserNotAllowed1[] = "randomusergoogle.com.xyz@gmail.com";
constexpr char kDeviceUserNotAllowed2[] = "deviceuser@disallowed_domain.com";
constexpr char kDeviceUserNotAllowed3[] = "randomuser@google.com.xyz@gmail.com";
constexpr char kDeviceUserInAllowList[] = "testuser@managedchrome.com";
constexpr char kUserInAllowedDomain[] = "someuser@managedchrome.com";
constexpr char kAffiliationID[] = "affiliation_id";

class ConnectivityUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    session_manager_ =
        std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  }

  // This function creates input request blob required to call
  // RetrievePolicyEx() function.
  std::vector<uint8_t> CreateExpectedDescriptorBlob(
      const login_manager::PolicyAccountType& type, const std::string& user) {
    login_manager::PolicyDescriptor descriptor;
    descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
    descriptor.set_account_id(user);

    if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_USER) {
      descriptor.set_account_type(
          login_manager::PolicyAccountType::ACCOUNT_TYPE_USER);
    } else if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE) {
      descriptor.set_account_type(
          login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE);
    } else {
      CHECK(false);
    }

    std::string descriptor_string = descriptor.SerializeAsString();
    return std::vector<uint8_t>(descriptor_string.begin(),
                                descriptor_string.end());
  }

  // This function creates Policy Fetch Response blob to simulate expected
  // response of RetrievePolicyEx() function call.
  std::vector<uint8_t> CreatePolicyFetchResponseBlob(
      const login_manager::PolicyAccountType& type,
      const std::string& affiliation_id,
      const std::string& connectivity_policy_val) {
    enterprise_management::PolicyData policy_data;
    enterprise_management::CloudPolicySettings user_policy_val;
    // Add policy required for connectivity fwdumps.
    user_policy_val.mutable_subproto1()
        ->mutable_userfeedbackwithlowleveldebugdataallowed()
        ->mutable_value()
        ->add_entries(connectivity_policy_val);
    std::string serialized_user_policy = user_policy_val.SerializeAsString();
    policy_data.set_policy_value(serialized_user_policy);

    if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_USER) {
      auto id = policy_data.add_user_affiliation_ids();
      *id = affiliation_id;
    } else if (type == login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE) {
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

  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyMock>
      session_manager_;
};

// Test that connectivity fwdump is allowed for a googler users.
TEST_F(ConnectivityUtilTest, IsConnectivityFwdumpAllowedGooglerUser) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceGoogleUser),
                       _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  EXPECT_TRUE(
      IsConnectivityFwdumpAllowed(session_manager_.get(), kDeviceGoogleUser));
}

// Test connectivity fwdump is allowed if user is in allowlist.
// Test also validates connectivity debug data collection with policy set.
TEST_F(ConnectivityUtilTest, IsConnectivityFwdumpAllowedForAllowedUser) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowList),
                       _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  EXPECT_TRUE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                          kDeviceUserInAllowList));
}

// Test connectivity fwdump is allowed if user is in managedchrome domain.
// Test also validates connectivity debug data collection with policy set.
TEST_F(ConnectivityUtilTest, IsConnectivityFwdumpAllowedForAllowedDomain) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kUserInAllowedDomain),
                       _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  EXPECT_TRUE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                          kUserInAllowedDomain));
}

// Test to ensure that no session manager proxy is correctly handled
// and causes no crashes.
TEST_F(ConnectivityUtilTest, IsConnectivityFwdumpAllowedNoSessionManager) {
  // IsConnectivityFwdumpAllowed() exits early and RetrievePolicyEx not
  // expected to be called.
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowList),
                       _, _, _))
      .Times(0);
  EXPECT_FALSE(IsConnectivityFwdumpAllowed(nullptr, kDeviceGoogleUser));
}

// Test to ensure that connectivity fwdump is not allowed for
// different types of not allowed users.
// Also test that Connectivity policy set for WiFi but User Not allowed.
TEST_F(ConnectivityUtilTest, IsConnectivityFwdumpAllowedUserNotAllowed) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowList),
                       _, _, _))
      .WillRepeatedly(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "wifi");
        return true;
      })));

  EXPECT_FALSE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                           kDeviceUserNotAllowed));

  EXPECT_FALSE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                           kDeviceUserNotAllowed1));

  EXPECT_FALSE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                           kDeviceUserNotAllowed2));

  EXPECT_FALSE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                           kDeviceUserNotAllowed3));
}

// UserFeedbackWithLowLevelDebugDataAllowed policy set for all
// connectivity domains.
TEST_F(ConnectivityUtilTest,
       IsConnectivityFwdumpAllowedConnectivityPolicySetForAllDomain) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowList),
                       _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "all");
        return true;
      })));

  EXPECT_TRUE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                          kDeviceUserInAllowList));
}

// UserFeedbackWithLowLevelDebugDataAllowed policy empty.
TEST_F(ConnectivityUtilTest,
       IsConnectivityFwdumpAllowedConnectivityPolicyEmpty) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowList),
                       _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "");
        return true;
      })));

  EXPECT_FALSE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                           kDeviceUserInAllowList));
}

// Connectivity policy not set but user allowed(googler or in allowlist).
TEST_F(ConnectivityUtilTest,
       IsConnectivityFwdumpAllowedConnectivityPolicyNotSetButUserAllowed) {
  // UserFeedbackWithLowLevelDebugDataAllowed policy set for wifi.
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceGoogleUser),
                       _, _, _))
      .WillOnce(WithArg<1>(Invoke([this](std::vector<uint8_t>* out_blob) {
        *out_blob = CreatePolicyFetchResponseBlob(
            login_manager::PolicyAccountType::ACCOUNT_TYPE_USER, kAffiliationID,
            "");
        return true;
      })));

  EXPECT_FALSE(
      IsConnectivityFwdumpAllowed(session_manager_.get(), kDeviceGoogleUser));
}

// Test to ensure the expected crash directory is created.
TEST_F(ConnectivityUtilTest, GetCorrectCrashDirectory) {
  Session test_session;
  test_session.username = kDeviceGoogleUser;
  test_session.userhash = "user_hash";

  std::optional<base::FilePath> got_dir =
      GetDaemonStoreFbPreprocessordDirectory(test_session);
  const char kExpectedDir[] =
      "/run/daemon-store/fbpreprocessord/user_hash/raw_dumps";
  EXPECT_TRUE(got_dir.has_value());
  EXPECT_EQ(kExpectedDir, got_dir->value());
}

// Test to ensure no unexpected value returned when Session is empty.
TEST_F(ConnectivityUtilTest, GetCorrectCrashDirectoryForNoSessions) {
  Session test_session;

  std::optional<base::FilePath> got_dir =
      GetDaemonStoreFbPreprocessordDirectory(test_session);
  EXPECT_FALSE(got_dir.has_value());
}

// Tests GetDaemonStoreFbPreprocessordDirectory returns
// not expected directory path.
TEST_F(ConnectivityUtilTest, GetIncorrectCrashDirectory) {
  Session test_session;
  test_session.username = kDeviceUserInAllowList;
  test_session.userhash = "user_hash";

  std::optional<base::FilePath> got_dir =
      GetDaemonStoreFbPreprocessordDirectory(test_session);
  const char kExpectedDir[] =
      "/run/daemon-store/fbpreprocessord/user_hash_unexpected";
  EXPECT_TRUE(got_dir.has_value());
  EXPECT_NE(kExpectedDir, got_dir->value());
}

// Test RetrievePolicyEx dbus call failure which leads to
// connectivity not allowed.
TEST_F(ConnectivityUtilTest,
       IsConnectivityFwdumpAllowedRetrievePolicyExDbusFailed) {
  EXPECT_CALL(
      *session_manager_.get(),
      RetrievePolicyEx(CreateExpectedDescriptorBlob(
                           login_manager::PolicyAccountType::ACCOUNT_TYPE_USER,
                           kDeviceUserInAllowList),
                       _, _, _))
      .WillOnce(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
        *error = brillo::Error::Create(FROM_HERE, "", "1",
                                       "Dbus failed on RetrivePolicyEx");
        return false;
      })));

  EXPECT_FALSE(IsConnectivityFwdumpAllowed(session_manager_.get(),
                                           kDeviceUserInAllowList));
}

// Test RetrievePrimarySession dbus call failure which leads to
// connectivity not allowed.
TEST_F(ConnectivityUtilTest, PrimarySessionDbusFailed) {
  brillo::ErrorPtr error;
  EXPECT_CALL(*session_manager_.get(), RetrievePrimarySession)
      .WillOnce(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
        *error =
            brillo::Error::Create(FROM_HERE, "", "1", "Failed active sessions");
        return false;
      })));
  std::optional<Session> test_session =
      GetPrimaryUserSession(session_manager_.get());
  EXPECT_FALSE(test_session.has_value());
}

// Tests GetPrimaryUserSession function and ensures that correct
// Session instance is built and returned.
TEST_F(ConnectivityUtilTest, GetPrimaryUserSession) {
  EXPECT_CALL(*session_manager_.get(), RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(
          Invoke([](std::string* username, std::string* sanitized) {
            *username = kDeviceGoogleUser;
            *sanitized = "user_hash";
            return true;
          })));

  std::optional<Session> test_session =
      GetPrimaryUserSession(session_manager_.get());
  ASSERT_TRUE(test_session.has_value());
  EXPECT_EQ(test_session->username, kDeviceGoogleUser);
  EXPECT_EQ(test_session->userhash, "user_hash");
}

// Tests GetPrimaryUserSession function to handle case where
// no user session is returned.
TEST_F(ConnectivityUtilTest, GetPrimaryUserSessionNoUserSessions) {
  EXPECT_CALL(*session_manager_.get(), RetrievePrimarySession)
      .WillOnce(WithArgs<0, 1>(Invoke(
          [](std::string* username, std::string* sanitized) { return true; })));

  std::optional<Session> test_session =
      GetPrimaryUserSession(session_manager_.get());
  EXPECT_FALSE(test_session.has_value());
}

}  // namespace connectivity_util

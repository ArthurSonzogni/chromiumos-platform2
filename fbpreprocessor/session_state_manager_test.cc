// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/session_state_manager.h"

#include <map>
#include <memory>
#include <vector>

#include <base/check.h>
#include <base/files/scoped_temp_dir.h>
#include <base/time/time.h>
#include <bindings/cloud_policy.pb.h>
#include <bindings/device_management_backend.pb.h>
#include <bindings/policy_common_definitions.pb.h>
#include <brillo/errors/error.h>
#include <debugd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <session_manager/dbus-proxy-mocks.h>

#include "fbpreprocessor/fake_manager.h"
#include "fbpreprocessor/firmware_dump.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace fbpreprocessor {
namespace {

constexpr char kManagedChromeTestUser[] = "user@managedchrome.com";

class MockObserver : public SessionStateManagerInterface::Observer {
 public:
  MOCK_METHOD(void, OnUserLoggedIn, (const std::string& user_hash));
  MOCK_METHOD(void, OnUserLoggedOut, ());
};

class SessionStateManagerTest : public testing::Test {
 protected:
  void SetUp() override {
    manager_ = std::make_unique<FakeManager>();
    manager_->Start(/*bus=*/nullptr);

    mock_session_manager_proxy_ = std::make_unique<
        NiceMock<org::chromium::SessionManagerInterfaceProxyMock>>();
    EXPECT_CALL(*mock_session_manager_proxy_,
                DoRegisterSessionStateChangedSignalHandler(_, _))
        .WillOnce(testing::SaveArg<0>(&session_state_changed_callback_));
    mock_debugd_proxy_ =
        std::make_unique<NiceMock<org::chromium::debugdProxyMock>>();

    session_state_manager_ = std::make_unique<SessionStateManager>(
        manager_.get(), mock_session_manager_proxy_.get(),
        mock_debugd_proxy_.get());

    CHECK(test_dir_.CreateUniqueTempDir());
    session_state_manager_->set_base_dir_for_test(test_dir_.GetPath());
    manager_->set_firmware_dumps_allowed(true);
  }

  void TearDown() override {}

  // Mocks the session state change signals
  void InvokeSessionStateChange(const std::string& session_state) {
    session_state_changed_callback_.Run(session_state);
    // After login, it takes a few (2) seconds for policy to be retrieved and
    // applied. Work around that by advancing the clock past that delay.
    constexpr base::TimeDelta kPolicyDelay = base::Seconds(5);
    manager_->AdvanceClock(kPolicyDelay);
    manager_->RunTasksUntilIdle();
  }

  void SetUpRetrievePrimarySession(const std::string& username,
                                   const std::string& userhash) {
    ON_CALL(*mock_session_manager_proxy_, RetrievePrimarySession(_, _, _, _))
        .WillByDefault([=](std::string* out_username, std::string* out_userhash,
                           brillo::ErrorPtr* out_error, int timeout_ms) {
          *out_username = username;
          *out_userhash = userhash;
          return true;
        });
  }

  void SetUpRetrieveActiveSessions(int num_sessions) {
    ON_CALL(*mock_session_manager_proxy_, RetrieveActiveSessions(_, _, _))
        .WillByDefault([=](std::map<std::string, std::string>* out_sessions,
                           brillo::ErrorPtr* out_error, int timeout_ms) {
          for (int i = 0; i < num_sessions; ++i) {
            // In the real world case the username/hash would be populated with
            // the same username/hash as calls like |RetrievePrimarySession()|.
            // However, we only use |RetrieveActiveSessions()| to know how many
            // concurrent sessions are active, so for simplicity we can populate
            // the sessions with unrelated username/hash.
            out_sessions->insert(
                {"user" + std::to_string(i), "hash" + std::to_string(i)});
          }
          return true;
        });
  }

  void SetUpClearFirmwareDumpBuffer(bool success) {
    ON_CALL(*mock_debugd_proxy_, ClearFirmwareDumpBufferAsync(_, _, _, _))
        .WillByDefault([=](uint32_t dump_type,
                           base::OnceCallback<void(bool)> cb,
                           base::OnceCallback<void(brillo::Error*)> err_cb,
                           int timeout_ms) { std::move(cb).Run(success); });
  }

  void SetUpRetrievePolicy(bool success,
                           bool wifi_allowed,
                           bool bluetooth_allowed) {
    ON_CALL(*mock_session_manager_proxy_, RetrievePolicyEx(_, _, _, _))
        .WillByDefault([=](const std::vector<uint8_t>& in_blob,
                           std::vector<uint8_t>* out_blob,
                           brillo::ErrorPtr* out_error, int timeout_ms) {
          if (!success) {
            return false;
          }
          enterprise_management::CloudPolicySettings user_policy;
          enterprise_management::StringListPolicyProto* fw_policy =
              user_policy.mutable_subproto1()
                  ->mutable_userfeedbackwithlowleveldebugdataallowed();
          if (wifi_allowed) {
            fw_policy->mutable_value()->add_entries("wifi");
          }
          if (bluetooth_allowed) {
            fw_policy->mutable_value()->add_entries("bluetooth");
          }

          enterprise_management::PolicyData policy_data;
          policy_data.set_policy_value(user_policy.SerializeAsString());
          enterprise_management::PolicyFetchResponse response;
          EXPECT_TRUE(
              policy_data.SerializeToString(response.mutable_policy_data()));
          auto serialized = response.SerializeAsString();
          out_blob->assign(serialized.begin(), serialized.end());
          return true;
        });
  }

  std::unique_ptr<FakeManager> manager_;
  std::unique_ptr<SessionStateManager> session_state_manager_;
  base::RepeatingCallback<void(const std::string& state)>
      session_state_changed_callback_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyMock>
      mock_session_manager_proxy_;
  std::unique_ptr<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  base::ScopedTempDir test_dir_;
};

TEST_F(SessionStateManagerTest, UserLoginWithAllowedPolicy) {
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  // UMA reports |kAllowed|.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            std::vector<int>({1}));
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.Bluetooth.Collection.Allowed"),
            std::vector<int>({1}));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, UserGooglerLoginWithAllowedPolicy) {
  // User has an @google.com domain which is in the allowlist.
  SetUpRetrievePrimarySession("user@google.com", "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");

  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, UserLoginFailToRetrievePolicy) {
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(false, true, true);
  InvokeSessionStateChange("started");

  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, UserLogout) {
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  SetUpClearFirmwareDumpBuffer(true);
  InvokeSessionStateChange("stopped");
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, UserLoginFailToClearBufferDisallowsCollection) {
  // User is in the allowlist, policy is set to "enabled", only 1 active
  // session, but the "clear buffer" operation failed -> disallow collection of
  // firmware dumps.
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(false);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");

  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, MultipleSessionsDisallowsCollection) {
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(2);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  // UMA reports |kDisallowedForMultipleSessions|.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            std::vector<int>({4}));
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.Bluetooth.Collection.Allowed"),
            std::vector<int>({4}));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, UserDomainNotInAllowListDisallowsCollection) {
  // User's account is in a domain that is not in the allowlist, expect that
  // collection is disallowed.
  SetUpRetrievePrimarySession("user@domain_not_in_allow_list.com", "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  // UMA reports |kDisallowedForUserDomain|.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            std::vector<int>({5}));
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.Bluetooth.Collection.Allowed"),
            std::vector<int>({5}));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, FinchDisabledDisallowsCollection) {
  manager_->set_firmware_dumps_allowed(false);
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  // UMA reports |kDisallowedByFinch|.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            std::vector<int>({3}));
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.Bluetooth.Collection.Allowed"),
            std::vector<int>({3}));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, PolicyDisablesWiFiCollection) {
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, false, true);
  InvokeSessionStateChange("started");
  // UMA reports |kDisallowedByPolicy| for WiFi.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            std::vector<int>({2}));
  // UMA reports |kAllowed| for BT.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.Bluetooth.Collection.Allowed"),
            std::vector<int>({1}));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, PolicyDisablesBTCollection) {
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, false);
  InvokeSessionStateChange("started");
  // UMA reports |kAllowed| for WiFi.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Collection.Allowed"),
            std::vector<int>({1}));
  // UMA reports |kDisallowedByPolicy| for BT.
  EXPECT_EQ(manager_->GetMetricCalls(
                "Platform.FbPreprocessor.Bluetooth.Collection.Allowed"),
            std::vector<int>({2}));
  EXPECT_TRUE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kWiFi));
  EXPECT_FALSE(session_state_manager_->FirmwareDumpsAllowedByPolicy(
      FirmwareDump::Type::kBluetooth));
}

TEST_F(SessionStateManagerTest, NotifyObserverOnUserLogin) {
  MockObserver observer;
  session_state_manager_->AddObserver(&observer);
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  EXPECT_CALL(observer, OnUserLoggedIn("user_hash")).Times(1);
  InvokeSessionStateChange("started");
  session_state_manager_->RemoveObserver(&observer);
}

TEST_F(SessionStateManagerTest, NotifyMultipleObserversOnUserLogin) {
  MockObserver observer1, observer2;
  session_state_manager_->AddObserver(&observer1);
  session_state_manager_->AddObserver(&observer2);
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  EXPECT_CALL(observer1, OnUserLoggedIn("user_hash")).Times(1);
  EXPECT_CALL(observer2, OnUserLoggedIn("user_hash")).Times(1);
  InvokeSessionStateChange("started");
  session_state_manager_->RemoveObserver(&observer1);
  session_state_manager_->RemoveObserver(&observer2);
}

TEST_F(SessionStateManagerTest, NotifyObserverOnUserLogout) {
  MockObserver observer;
  session_state_manager_->AddObserver(&observer);
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  EXPECT_CALL(observer, OnUserLoggedOut()).Times(1);
  InvokeSessionStateChange("stopped");
  session_state_manager_->RemoveObserver(&observer);
}

TEST_F(SessionStateManagerTest, NotifyMultipleObserversOnUserLogout) {
  MockObserver observer1, observer2;
  session_state_manager_->AddObserver(&observer1);
  session_state_manager_->AddObserver(&observer2);
  SetUpRetrievePrimarySession(kManagedChromeTestUser, "user_hash");
  SetUpRetrieveActiveSessions(1);
  SetUpClearFirmwareDumpBuffer(true);
  SetUpRetrievePolicy(true, true, true);
  InvokeSessionStateChange("started");
  EXPECT_CALL(observer1, OnUserLoggedOut()).Times(1);
  EXPECT_CALL(observer2, OnUserLoggedOut()).Times(1);
  InvokeSessionStateChange("stopped");
  session_state_manager_->RemoveObserver(&observer1);
  session_state_manager_->RemoveObserver(&observer2);
}

}  // namespace
}  // namespace fbpreprocessor

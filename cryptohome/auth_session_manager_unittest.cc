// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <base/unguessable_token.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>

#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session_map.h"

using base::test::TaskEnvironment;
using ::testing::_;
using ::testing::ByMove;
using ::testing::Eq;
using ::testing::IsNull;
using testing::NiceMock;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;

namespace cryptohome {

class AuthSessionManagerTest : public ::testing::Test {
 public:
  AuthSessionManagerTest() = default;
  ~AuthSessionManagerTest() override = default;
  AuthSessionManagerTest(const AuthSessionManagerTest&) = delete;
  AuthSessionManagerTest& operator=(AuthSessionManagerTest&) = delete;

 protected:
  TaskEnvironment task_environment_{
      TaskEnvironment::TimeSource::MOCK_TIME,
      TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
  Crypto crypto_{&hwsec_, &pinweaver_, &cryptohome_keys_manager_, nullptr};
  UserSessionMap user_session_map_;
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  AuthSessionManager auth_session_manager_{&crypto_,
                                           &platform_,
                                           &user_session_map_,
                                           &keyset_management_,
                                           &auth_block_utility_,
                                           &auth_factor_manager_,
                                           &user_secret_stash_storage_};
};

TEST_F(AuthSessionManagerTest, CreateFindRemove) {
  AuthSession* auth_session = auth_session_manager_.CreateAuthSession(
      "foo@example.com", 0, AuthIntent::kDecrypt);
  ASSERT_THAT(auth_session, NotNull());
  base::UnguessableToken token = auth_session->token();
  ASSERT_THAT(auth_session_manager_.FindAuthSession(token), Eq(auth_session));
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(token));
  ASSERT_THAT(auth_session_manager_.FindAuthSession(token), IsNull());

  // Repeat with serialized_token overload.
  auth_session = auth_session_manager_.CreateAuthSession("foo@example.com", 0,
                                                         AuthIntent::kDecrypt);
  ASSERT_THAT(auth_session, NotNull());
  std::string serialized_token = auth_session->serialized_token();
  ASSERT_THAT(auth_session_manager_.FindAuthSession(serialized_token),
              Eq(auth_session));
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(serialized_token));
  ASSERT_THAT(auth_session_manager_.FindAuthSession(serialized_token),
              IsNull());
}

TEST_F(AuthSessionManagerTest, CreateExpire) {
  AuthSession* auth_session = auth_session_manager_.CreateAuthSession(
      "foo@example.com", 0, AuthIntent::kDecrypt);
  ASSERT_THAT(auth_session, NotNull());
  base::UnguessableToken token = auth_session->token();
  ASSERT_THAT(auth_session_manager_.FindAuthSession(token), Eq(auth_session));
  auth_session->SetAuthSessionAsAuthenticated(kAllAuthIntents);
  task_environment_.FastForwardUntilNoTasksRemain();
  ASSERT_THAT(auth_session_manager_.FindAuthSession(token), IsNull());
}

TEST_F(AuthSessionManagerTest, RemoveNonExisting) {
  EXPECT_FALSE(
      auth_session_manager_.RemoveAuthSession(base::UnguessableToken()));
  EXPECT_FALSE(auth_session_manager_.RemoveAuthSession("non-existing-token"));
}

TEST_F(AuthSessionManagerTest, FlagPassing) {
  // Arrange.
  AuthSession* auth_session = auth_session_manager_.CreateAuthSession(
      "foo@example.com", 0, AuthIntent::kDecrypt);
  ASSERT_TRUE(auth_session);
  AuthSession* ephemeral_auth_session = auth_session_manager_.CreateAuthSession(
      "foo@example.com", user_data_auth::AUTH_SESSION_FLAGS_EPHEMERAL_USER,
      AuthIntent::kDecrypt);
  ASSERT_TRUE(ephemeral_auth_session);

  // Assert
  EXPECT_FALSE(auth_session->ephemeral_user());
  EXPECT_TRUE(ephemeral_auth_session->ephemeral_user());
}

TEST_F(AuthSessionManagerTest, IntentPassing) {
  // Arrange.
  AuthSession* decryption_auth_session =
      auth_session_manager_.CreateAuthSession("foo@example.com", 0,
                                              AuthIntent::kDecrypt);
  ASSERT_TRUE(decryption_auth_session);
  AuthSession* verification_auth_session =
      auth_session_manager_.CreateAuthSession("foo@example.com", 0,
                                              AuthIntent::kVerifyOnly);
  ASSERT_TRUE(verification_auth_session);

  // Assert.
  EXPECT_EQ(decryption_auth_session->auth_intent(), AuthIntent::kDecrypt);
  EXPECT_EQ(verification_auth_session->auth_intent(), AuthIntent::kVerifyOnly);
}

}  // namespace cryptohome

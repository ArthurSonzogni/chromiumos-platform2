// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/bind.h>
#include <base/test/power_monitor_test.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <base/unguessable_token.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_session/user_session_map.h"

namespace cryptohome {
namespace {

using ::base::test::TaskEnvironment;
using ::base::test::TestFuture;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ByMove;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

class AuthSessionManagerTest : public ::testing::Test {
 protected:
  // Helper function that will try and "take" control of an auth session in a
  // synchronous manner. If the session is in use then it will immediately
  // return null.
  template <typename T>
  std::optional<InUseAuthSession> TryTakeAuthSession(const T& token) {
    TestFuture<InUseAuthSession> session_future;
    auth_session_manager_.RunWhenAvailable(token, session_future.GetCallback());
    if (session_future.IsReady()) {
      return session_future.Take();
    }
    return std::nullopt;
  }

  // Version of TryTake that assumes that the session is available and
  // CHECK-fails if it is not. This makes code easier to read but you should
  // only use it in tests where it is easy to see that the session is not
  // already in use. Ideally we'd use ASSERT instead of CHECK but that does not
  // work with functions that do not return void.
  template <typename T>
  InUseAuthSession TakeAuthSession(const T& token) {
    std::optional<InUseAuthSession> session = TryTakeAuthSession(token);
    CHECK(session.has_value());
    return std::move(*session);
  }

  const Username kUsername{"foo@example.com"};

  base::test::ScopedPowerMonitorTestSource test_power_monitor_;
  TaskEnvironment task_environment_{
      TaskEnvironment::TimeSource::MOCK_TIME,
      TaskEnvironment::ThreadPoolExecutionMode::QUEUED};

  NiceMock<MockPlatform> platform_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_{&hwsec_, &hwsec_pw_manager_, &cryptohome_keys_manager_,
                 nullptr};
  UssStorage uss_storage_{&platform_};
  UserSessionMap user_session_map_;
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  std::unique_ptr<FingerprintAuthBlockService> fp_service_{
      FingerprintAuthBlockService::MakeNullService()};
  AuthFactorDriverManager auth_factor_driver_manager_{
      &platform_,
      &crypto_,
      &uss_storage_,
      AsyncInitPtr<ChallengeCredentialsHelper>(nullptr),
      nullptr,
      fp_service_.get(),
      AsyncInitPtr<BiometricsAuthBlockService>(nullptr)};
  AuthFactorManager auth_factor_manager_{&platform_};
  FakeFeaturesForTesting features_;
  AuthSession::BackingApis backing_apis_{&crypto_,
                                         &platform_,
                                         &user_session_map_,
                                         &keyset_management_,
                                         &auth_block_utility_,
                                         &auth_factor_driver_manager_,
                                         &auth_factor_manager_,
                                         &uss_storage_,
                                         &features_.async};
  AuthSessionManager auth_session_manager_{backing_apis_};
};

TEST_F(AuthSessionManagerTest, CreateRemove) {
  base::UnguessableToken token = auth_session_manager_.CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt);

  // After InUseAuthSession is freed, then AuthSessionManager can operate on the
  // token and remove it.
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(token));
  InUseAuthSession in_use_auth_session = TakeAuthSession(token);
  ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());

  // Repeat with serialized_token overload.
  token = auth_session_manager_.CreateAuthSession(kUsername, 0,
                                                  AuthIntent::kDecrypt);
  std::string serialized_token =
      *AuthSession::GetSerializedStringFromToken(token);

  // Should succeed now that AuthSessionManager owns the AuthSession.
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(serialized_token));
  in_use_auth_session = TakeAuthSession(serialized_token);
  ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());
}

TEST_F(AuthSessionManagerTest, CreateExpire) {
  base::UnguessableToken tokens[2];

  // Create a pair of auth sessions. Before they're authenticated they should
  // have infinite time remaining.
  for (auto& token : tokens) {
    token = auth_session_manager_.CreateAuthSession(kUsername, 0,
                                                    AuthIntent::kDecrypt);
    InUseAuthSession auth_session = TakeAuthSession(token);
    ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
  }
  for (const auto& token : tokens) {
    InUseAuthSession in_use_auth_session = TakeAuthSession(token);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session.GetRemainingTime().is_max(), IsTrue());
  }

  // Authenticate the sessions. Theys should now have finite timeouts.
  for (auto& token : tokens) {
    InUseAuthSession in_use_auth_session = TakeAuthSession(token);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session->OnUserCreated(), IsOk());
    EXPECT_THAT(
        in_use_auth_session->authorized_intents(),
        UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  }
  for (const auto& token : tokens) {
    InUseAuthSession in_use_auth_session = TakeAuthSession(token);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(
        in_use_auth_session.GetRemainingTime(),
        AllOf(Gt(base::TimeDelta()), Le(AuthSessionManager::kAuthTimeout)));
  }

  // Advance the clock by timeout. This should expire all the sessions.
  task_environment_.FastForwardBy(AuthSessionManager::kAuthTimeout);

  // After expiration the sessions should be gone.
  for (const auto& token : tokens) {
    InUseAuthSession in_use_auth_session = TakeAuthSession(token);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());
  }
}

TEST_F(AuthSessionManagerTest, ExtendExpire) {
  base::UnguessableToken tokens[2];

  // Create and set up a pair of auth sessions, setting them to authenticated so
  // that they can eventually get expired.
  for (auto& token : tokens) {
    token = auth_session_manager_.CreateAuthSession(kUsername, 0,
                                                    AuthIntent::kDecrypt);
    InUseAuthSession auth_session = TakeAuthSession(token);
    ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
    EXPECT_THAT(
        auth_session->authorized_intents(),
        UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  }

  // Before expiration we should be able to look up the sessions again.
  for (const auto& token : tokens) {
    InUseAuthSession in_use_auth_session = TakeAuthSession(token);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(
        in_use_auth_session.GetRemainingTime(),
        AllOf(Gt(base::TimeDelta()), Le(AuthSessionManager::kAuthTimeout)));
  }

  // Extend the first session to seven minutes.
  {
    InUseAuthSession in_use_auth_session = TakeAuthSession(tokens[0]);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session.ExtendTimeout(
                    AuthSessionManager::kAuthTimeout + base::Minutes(2)),
                IsOk());
    EXPECT_THAT(in_use_auth_session.GetRemainingTime(),
                AllOf(Gt(base::TimeDelta()),
                      Le(AuthSessionManager::kAuthTimeout + base::Minutes(2))));
  }

  // Extend the second session to two minutes (this is a no-op)
  {
    InUseAuthSession in_use_auth_session = TakeAuthSession(tokens[1]);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session.ExtendTimeout(base::Minutes(2)), IsOk());
    EXPECT_THAT(
        in_use_auth_session.GetRemainingTime(),
        AllOf(Gt(base::TimeDelta()), Le(AuthSessionManager::kAuthTimeout)));
  }

  // Move the time forward by timeout plus one minute.
  task_environment_.FastForwardBy(base::Minutes(2));

  // Both Session should be good be good.
  {
    InUseAuthSession in_use_auth_session = TakeAuthSession(tokens[0]);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session.GetRemainingTime(),
                AllOf(Gt(base::TimeDelta()), Le(base::Minutes(5))));
  }
  {
    InUseAuthSession in_use_auth_session = TakeAuthSession(tokens[1]);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session.GetRemainingTime(),
                AllOf(Gt(base::TimeDelta()), Le(base::Minutes(3))));
  }

  // Move the time forward by timeout by another four minutes. This should
  // timeout the second session (original timeout) but not the first (added two
  // minutes).
  task_environment_.FastForwardBy(base::Minutes(4));
  {
    InUseAuthSession in_use_auth_session = TakeAuthSession(tokens[0]);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(in_use_auth_session.GetRemainingTime(),
                AllOf(Gt(base::TimeDelta()), Le(base::Minutes(1))));
  }
  {
    InUseAuthSession in_use_auth_session = TakeAuthSession(tokens[1]);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());
  }

  // Move time forward by another minute. This should expire the other session.
  task_environment_.FastForwardBy(base::Minutes(1));

  // Now both sessions should be gone.
  for (const auto& token : tokens) {
    InUseAuthSession in_use_auth_session = TakeAuthSession(token);
    ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());
  }
}

TEST_F(AuthSessionManagerTest, CreateExpireAfterPowerSuspend) {
  // Create and authenticate a session.
  base::UnguessableToken token = auth_session_manager_.CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt);
  {
    InUseAuthSession auth_session = TakeAuthSession(token);
    ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
    EXPECT_THAT(
        auth_session->authorized_intents(),
        UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
    EXPECT_THAT(
        auth_session.GetRemainingTime(),
        AllOf(Gt(base::TimeDelta()), Le(AuthSessionManager::kAuthTimeout)));
  }

  // Have the device power off for 30 seconds
  constexpr auto time_passed = base::Seconds(30);
  test_power_monitor_.Suspend();
  task_environment_.SuspendedFastForwardBy(time_passed);
  test_power_monitor_.Resume();
  {
    InUseAuthSession auth_session = TakeAuthSession(token);
    ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(auth_session.GetRemainingTime(),
                AllOf(Gt(base::TimeDelta()),
                      Le(AuthSessionManager::kAuthTimeout - time_passed)));
  }

  // Advance the clock the rest of the way.
  task_environment_.FastForwardBy(AuthSessionManager::kAuthTimeout -
                                  time_passed);

  // After expiration the session should be gone.
  {
    InUseAuthSession auth_session = TakeAuthSession(token);
    ASSERT_THAT(auth_session.AuthSessionStatus(), NotOk());
  }
}

TEST_F(AuthSessionManagerTest, AddRemove) {
  base::UnguessableToken token = auth_session_manager_.CreateAuthSession(
      AuthSession::Params{.username = kUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kDecrypt,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = false,
                          .auth_factor_map = AuthFactorMap()});

  // After InUseAuthSession is freed, then AuthSessionManager can operate on the
  // token and remove it.
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(token));
  InUseAuthSession in_use_auth_session = TakeAuthSession(token);
  ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());

  // Repeat with serialized_token overload.
  token = auth_session_manager_.CreateAuthSession(
      AuthSession::Params{.username = kUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kDecrypt,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = false,
                          .auth_factor_map = AuthFactorMap()});
  std::string serialized_token =
      *AuthSession::GetSerializedStringFromToken(token);

  // Should succeed now that AuthSessionManager owns the AuthSession.
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(serialized_token));
  in_use_auth_session = TakeAuthSession(serialized_token);
  ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());
}

TEST_F(AuthSessionManagerTest, AddAndWaitRemove) {
  base::UnguessableToken token;
  bool is_called = false;
  InUseAuthSession saved_session;
  auto callback = [&saved_session, &is_called](InUseAuthSession auth_session) {
    is_called = true;
    saved_session = std::move(auth_session);
  };
  TestFuture<InUseAuthSession> future;

  // Start scope for first InUseAuthSession
  {
    token = auth_session_manager_.CreateAuthSession(
        AuthSession::Params{.username = kUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()});
    TestFuture<InUseAuthSession> created_future;
    auth_session_manager_.RunWhenAvailable(token, created_future.GetCallback());
    InUseAuthSession auth_session = created_future.Take();
    ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());

    // RunWhenAvailable on the same token will not trigger the callback
    // directly, but wait for the session is not in use instead.
    auth_session_manager_.RunWhenAvailable(
        token, base::BindLambdaForTesting(callback));
    EXPECT_FALSE(is_called);

    // |future| will be queued behind |callback|.
    auth_session_manager_.RunWhenAvailable(token, future.GetCallback());
    EXPECT_FALSE(future.IsReady());

    // Scope ends here to free the InUseAuthSession, after this |future| will
    // be executed.
  }

  ASSERT_TRUE(is_called);
  EXPECT_THAT(saved_session.AuthSessionStatus(), IsOk());
  EXPECT_FALSE(future.IsReady());

  // If we remove the token now, the callback should be called with a
  // non-existing auth session.
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(token));
  ASSERT_TRUE(future.IsReady());
  EXPECT_THAT(future.Get().AuthSessionStatus(), NotOk());
}

TEST_F(AuthSessionManagerTest, RemoveNonExisting) {
  EXPECT_FALSE(
      auth_session_manager_.RemoveAuthSession(base::UnguessableToken()));
  EXPECT_FALSE(auth_session_manager_.RemoveAuthSession("non-existing-token"));
}

TEST_F(AuthSessionManagerTest, FlagPassing) {
  // Arrange.
  base::UnguessableToken session_token =
      auth_session_manager_.CreateAuthSession(kUsername, 0,
                                              AuthIntent::kDecrypt);
  InUseAuthSession auth_session = TakeAuthSession(session_token);
  base::UnguessableToken ephemeral_session_token =
      auth_session_manager_.CreateAuthSession(
          kUsername, user_data_auth::AUTH_SESSION_FLAGS_EPHEMERAL_USER,
          AuthIntent::kDecrypt);
  InUseAuthSession ephemeral_auth_session =
      TakeAuthSession(ephemeral_session_token);

  // Assert
  EXPECT_FALSE(auth_session->ephemeral_user());
  EXPECT_TRUE(ephemeral_auth_session->ephemeral_user());
}

TEST_F(AuthSessionManagerTest, IntentPassing) {
  // Arrange.
  base::UnguessableToken decryption_session_token =
      auth_session_manager_.CreateAuthSession(kUsername, 0,
                                              AuthIntent::kDecrypt);
  InUseAuthSession decryption_auth_session =
      TakeAuthSession(decryption_session_token);
  base::UnguessableToken verification_session_token =
      auth_session_manager_.CreateAuthSession(kUsername, 0,
                                              AuthIntent::kVerifyOnly);
  InUseAuthSession verification_auth_session =
      TakeAuthSession(verification_session_token);

  // Assert.
  EXPECT_EQ(decryption_auth_session->auth_intent(), AuthIntent::kDecrypt);
  EXPECT_EQ(verification_auth_session->auth_intent(), AuthIntent::kVerifyOnly);
}

TEST_F(AuthSessionManagerTest, AddFindUnMount) {
  // Start scope for first InUseAuthSession
  base::UnguessableToken token = auth_session_manager_.CreateAuthSession(
      AuthSession::Params{.username = kUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kDecrypt,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = false,
                          .auth_factor_map = AuthFactorMap()});

  // After InUseAuthSession is freed, then AuthSessionManager can operate on the
  // token and remove it.
  EXPECT_TRUE(auth_session_manager_.RemoveAuthSession(token));
  InUseAuthSession in_use_auth_session = TakeAuthSession(token);
  ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());

  // Repeat with serialized_token overload.
  token = auth_session_manager_.CreateAuthSession(
      AuthSession::Params{.username = kUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kDecrypt,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = false,
                          .auth_factor_map = AuthFactorMap()});
  std::string serialized_token =
      *AuthSession::GetSerializedStringFromToken(token);

  // Should succeed now that AuthSessionManager owns the AuthSession.
  auth_session_manager_.RemoveAllAuthSessions();
  in_use_auth_session = TakeAuthSession(serialized_token);
  ASSERT_THAT(in_use_auth_session.AuthSessionStatus(), NotOk());
}

}  // namespace
}  // namespace cryptohome

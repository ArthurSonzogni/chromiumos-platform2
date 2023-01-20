// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for `AuthSession`. Unlike auth_session_unittest.cc, uses TPM
// simulator and minimal mocking in order to be able to verify inter-class
// interactions.

#include <memory>
#include <string>
#include <tuple>

#include <base/callback_helpers.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <featured/fake_platform_features.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/backend/mock_backend.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/frontend/pinweaver/frontend.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/vault_keyset_factory.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::Combine;
using ::testing::NiceMock;
using ::testing::ValuesIn;

constexpr AuthFactorStorageType kAllAuthFactorStorageTypes[] = {
    AuthFactorStorageType::kVaultKeyset,
    AuthFactorStorageType::kUserSecretStash,
};

constexpr char kUsername[] = "foo@example.com";

constexpr char kPasswordLabel[] = "fake-password-label";
constexpr char kPassword[] = "fake-password";

CryptohomeStatus RunAddAuthFactor(
    const user_data_auth::AddAuthFactorRequest& request,
    AuthSession& auth_session) {
  TestFuture<CryptohomeStatus> future;
  auth_session.AddAuthFactor(request, future.GetCallback());
  return future.Take();
}

CryptohomeStatus RunAuthenticateAuthFactor(
    const user_data_auth::AuthenticateAuthFactorRequest& request,
    AuthSession& auth_session) {
  TestFuture<CryptohomeStatus> future;
  auth_session.AuthenticateAuthFactor(request, future.GetCallback());
  return future.Take();
}

CryptohomeStatus AddPasswordFactor(const std::string& label,
                                   const std::string& password,
                                   AuthSession& auth_session) {
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& factor = *request.mutable_auth_factor();
  factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  factor.set_label(label);
  factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(password);
  return RunAddAuthFactor(request, auth_session);
}

CryptohomeStatus AuthenticatePasswordFactor(const std::string& label,
                                            const std::string& password,
                                            AuthSession& auth_session) {
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(label);
  request.mutable_auth_input()->mutable_password_input()->set_secret(password);
  return RunAuthenticateAuthFactor(request, auth_session);
}

// Fixture for testing `AuthSession` against TPM simulator and real
// implementations of auth blocks, UserSecretStash and VaultKeysets.
//
// This integration-like test is more expensive, but allows to check the code
// passes data and uses other class APIs correctly.
class AuthSessionWithTpmSimulatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // TODO(b/254864841): Remove this after le_credential code is migrated to
    // use `Platform` instead of direct file operations in system-global paths.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    crypto_.set_le_manager_for_testing(
        std::make_unique<LECredentialManagerImpl>(
            hwsec_pinweaver_frontend_.get(),
            temp_dir_.GetPath().AppendASCII("low_entropy_creds")));

    // TODO(b/266217791): The simulator factory should instead do it itself.
    ON_CALL(hwsec_simulator_factory_.GetMockBackend().GetMock().vendor,
            GetManufacturer)
        .WillByDefault(ReturnValue(0x43524F53));

    crypto_.Init();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  // TPM simulator objects.
  hwsec::Tpm2SimulatorFactoryForTest hwsec_simulator_factory_;
  std::unique_ptr<hwsec::CryptohomeFrontend> hwsec_cryptohome_frontend_ =
      hwsec_simulator_factory_.GetCryptohomeFrontend();
  std::unique_ptr<hwsec::PinWeaverFrontend> hwsec_pinweaver_frontend_ =
      hwsec_simulator_factory_.GetPinWeaverFrontend();
  std::unique_ptr<hwsec::RecoveryCryptoFrontend>
      hwsec_recovery_crypto_frontend_ =
          hwsec_simulator_factory_.GetRecoveryCryptoFrontend();

  // TODO(b/254864841): Remove this after le_credential code is migrated to use
  // `Platform` instead of direct file operations.
  base::ScopedTempDir temp_dir_;

  // AuthSession dependencies.
  FakePlatform platform_;
  CryptohomeKeysManager cryptohome_keys_manager_{
      hwsec_cryptohome_frontend_.get(), &platform_};
  Crypto crypto_{hwsec_cryptohome_frontend_.get(),
                 hwsec_pinweaver_frontend_.get(), &cryptohome_keys_manager_,
                 hwsec_recovery_crypto_frontend_.get()};
  UserSessionMap user_session_map_;
  KeysetManagement keyset_management_{&platform_, &crypto_,
                                      std::make_unique<VaultKeysetFactory>()};
  AuthBlockUtilityImpl auth_block_utility_{
      &keyset_management_, &crypto_, &platform_,
      FingerprintAuthBlockService::MakeNullService()};
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
  scoped_refptr<NiceMock<dbus::MockBus>> dbus_bus_ =
      base::MakeRefCounted<NiceMock<dbus::MockBus>>(dbus::Bus::Options());
  feature::FakePlatformFeatures platform_features_{dbus_bus_};

  AuthSession::BackingApis backing_apis_{&crypto_,
                                         &platform_,
                                         &user_session_map_,
                                         &keyset_management_,
                                         &auth_block_utility_,
                                         &auth_factor_manager_,
                                         &user_secret_stash_storage_};
};

// Parameterized fixture for tests that should work regardless of the
// UserSecretStash migration state, i.e. for all 4 combinations (VK/USS used
// initially/finally).
//
// Note that this kind of test skips this combination: USS is enabled for new
// users but the USS migration of the existing users is disabled.
class AuthSessionWithTpmSimulatorUssMigrationAgnosticTest
    : public AuthSessionWithTpmSimulatorTest,
      public ::testing::WithParamInterface<
          std::tuple<AuthFactorStorageType, AuthFactorStorageType>> {
 protected:
  static AuthFactorStorageType storage_type_initially() {
    return std::get<0>(GetParam());
  }
  static AuthFactorStorageType storage_type_finally() {
    return std::get<1>(GetParam());
  }

  // Configures the experiment states to the desired storage type.
  void SetStorageType(AuthFactorStorageType storage_type) {
    // Decide whether to enable both of USS experiments (for new users and for
    // existing ones). The test doesn't support switching them in isolation.
    bool enable_uss = storage_type == AuthFactorStorageType::kUserSecretStash;

    // First destroy the old override, if present, as having two overrides with
    // non-nested lifetimes isn't supported.
    uss_experiment_override_.reset();
    uss_experiment_override_ =
        std::make_unique<SetUssExperimentOverride>(enable_uss);

    platform_features_.SetEnabled(kCrOSLateBootMigrateToUserSecretStash.name,
                                  enable_uss);
  }

  // Aliases to `SetStorageType()` that call it with the corresponding test
  // parameter.
  void SetToInitialStorageType() { SetStorageType(storage_type_initially()); }
  void SetToFinalStorageType() { SetStorageType(storage_type_finally()); }

 private:
  std::unique_ptr<SetUssExperimentOverride> uss_experiment_override_;
};

INSTANTIATE_TEST_SUITE_P(
    All,
    AuthSessionWithTpmSimulatorUssMigrationAgnosticTest,
    Combine(ValuesIn(kAllAuthFactorStorageTypes),
            ValuesIn(kAllAuthFactorStorageTypes)),
    [](auto info) {
      // Return human-readable parameterized test name. Use caps in order to
      // clearly separate lowercase words visually.
      return base::StringPrintf(
          "%sTHEN%s",
          AuthFactorStorageTypeToDebugString(std::get<0>(info.param)),
          AuthFactorStorageTypeToDebugString(std::get<1>(info.param)));
    });

// Test that authentication via a previously added password works.
TEST_P(AuthSessionWithTpmSimulatorUssMigrationAgnosticTest,
       AuthenticateViaPassword) {
  auto create_auth_session = [this]() {
    return AuthSession::Create(
        kUsername, user_data_auth::AUTH_SESSION_FLAGS_NONE,
        AuthIntent::kDecrypt,
        /*on_timeout=*/base::DoNothing(), &platform_features_, backing_apis_);
  };

  // Arrange.
  // Configure the creation via USS or VK, depending on the first test
  // parameter.
  SetToInitialStorageType();
  {
    // Create a user with a password factor.
    std::unique_ptr<AuthSession> auth_session = create_auth_session();
    ASSERT_TRUE(auth_session);
    EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
    EXPECT_THAT(AddPasswordFactor(kPasswordLabel, kPassword, *auth_session),
                IsOk());
  }
  // Configure whether authenticating via USS is allowed, or VK should be used
  // (regardless of whether it's backup or not).
  SetToFinalStorageType();
  // Create a new AuthSession for authentication.
  std::unique_ptr<AuthSession> auth_session = create_auth_session();
  ASSERT_TRUE(auth_session);

  // Act.
  CryptohomeStatus auth_status =
      AuthenticatePasswordFactor(kPasswordLabel, kPassword, *auth_session);

  // Assert.
  EXPECT_THAT(auth_status, IsOk());
}

}  // namespace
}  // namespace cryptohome

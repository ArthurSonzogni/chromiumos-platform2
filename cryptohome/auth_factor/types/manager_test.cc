// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/manager.h"

#include <base/functional/callback.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/auth_blocks/cryptorecovery/service.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_fingerprint_manager.h"
#include "cryptohome/mock_signalling.h"
#include "cryptohome/signalling.h"
#include "cryptohome/user_secret_stash/storage.h"

namespace cryptohome {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::Ref;
using ::testing::Return;

class AuthFactorDriverManagerTest : public ::testing::Test {
 protected:
  // Mocks for all of the manager dependencies.
  libstorage::MockPlatform platform_;
  hwsec::MockCryptohomeFrontend hwsec_;
  hwsec::MockPinWeaverManagerFrontend hwsec_pw_manager_;
  hwsec::MockRecoveryCryptoFrontend hwsec_recovery_crypto_;
  MockCryptohomeKeysManager cryptohome_keys_manager_;
  Crypto crypto_{&hwsec_, &hwsec_pw_manager_, &cryptohome_keys_manager_,
                 &hwsec_recovery_crypto_};
  MockFingerprintManager fp_manager_;
  MockSignalling signalling_;
  UssStorage uss_storage_{&platform_};
  UssManager uss_manager_{uss_storage_};
  CryptohomeRecoveryAuthBlockService cr_service_{&platform_,
                                                 &hwsec_recovery_crypto_};
  FingerprintAuthBlockService fp_service_{
      AsyncInitPtr<FingerprintManager>(&fp_manager_),
      AsyncInitPtr<SignallingInterface>(&signalling_)};
  FakeFeaturesForTesting features_;

  // A real version of the manager, using mock inputs.
  AuthFactorDriverManager manager_{
      &platform_,      &crypto_,
      &uss_manager_,   AsyncInitPtr<ChallengeCredentialsHelper>(nullptr),
      nullptr,         &cr_service_,
      &fp_service_,    AsyncInitPtr<BiometricsAuthBlockService>(nullptr),
      &features_.async};
};

TEST_F(AuthFactorDriverManagerTest, GetDriverIsSameForConstAndNonconst) {
  const auto& const_manager = manager_;

  EXPECT_THAT(manager_.GetDriver(AuthFactorType::kPassword),
              Ref(const_manager.GetDriver(AuthFactorType::kPassword)));
  EXPECT_THAT(manager_.GetDriver(AuthFactorType::kPin),
              Ref(const_manager.GetDriver(AuthFactorType::kPin)));
  EXPECT_THAT(
      manager_.GetDriver(AuthFactorType::kCryptohomeRecovery),
      Ref(const_manager.GetDriver(AuthFactorType::kCryptohomeRecovery)));
  EXPECT_THAT(manager_.GetDriver(AuthFactorType::kKiosk),
              Ref(const_manager.GetDriver(AuthFactorType::kKiosk)));
  EXPECT_THAT(manager_.GetDriver(AuthFactorType::kSmartCard),
              Ref(const_manager.GetDriver(AuthFactorType::kSmartCard)));
  EXPECT_THAT(manager_.GetDriver(AuthFactorType::kLegacyFingerprint),
              Ref(const_manager.GetDriver(AuthFactorType::kLegacyFingerprint)));
  EXPECT_THAT(manager_.GetDriver(AuthFactorType::kFingerprint),
              Ref(const_manager.GetDriver(AuthFactorType::kFingerprint)));

  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::PrepareRequirement. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, PrepareRequirement) {
  auto prepare_req = [this](AuthFactorType type,
                            AuthFactorPreparePurpose purpose) {
    return manager_.GetDriver(type).GetPrepareRequirement(purpose);
  };

  EXPECT_EQ(prepare_req(AuthFactorType::kPassword,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(prepare_req(AuthFactorType::kPin,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(prepare_req(AuthFactorType::kCryptohomeRecovery,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(prepare_req(AuthFactorType::kKiosk,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(prepare_req(AuthFactorType::kSmartCard,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(prepare_req(AuthFactorType::kLegacyFingerprint,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kOnce);
  EXPECT_EQ(prepare_req(AuthFactorType::kFingerprint,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kOnce);
  EXPECT_EQ(prepare_req(AuthFactorType::kUnspecified,
                        AuthFactorPreparePurpose::kPrepareAddAuthFactor),
            AuthFactorDriver::PrepareRequirement::kNone);

  EXPECT_EQ(
      prepare_req(AuthFactorType::kPassword,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kPin,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kCryptohomeRecovery,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kEach);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kKiosk,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kSmartCard,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kNone);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kLegacyFingerprint,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kOnce);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kFingerprint,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kEach);
  EXPECT_EQ(
      prepare_req(AuthFactorType::kUnspecified,
                  AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor),
      AuthFactorDriver::PrepareRequirement::kNone);
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::IsFullAuthSupported. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, IsFullAuthSupported) {
  auto decrypt_allowed = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsFullAuthSupported(AuthIntent::kDecrypt);
  };
  auto vonly_allowed = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsFullAuthSupported(
        AuthIntent::kVerifyOnly);
  };
  auto webauthn_allowed = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsFullAuthSupported(AuthIntent::kWebAuthn);
  };
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(false));

  EXPECT_THAT(decrypt_allowed(AuthFactorType::kPassword), IsTrue());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kCryptohomeRecovery), IsTrue());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kKiosk), IsTrue());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kSmartCard), IsTrue());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(vonly_allowed(AuthFactorType::kPassword), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kCryptohomeRecovery), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kKiosk), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kSmartCard), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(webauthn_allowed(AuthFactorType::kPassword), IsTrue());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kCryptohomeRecovery), IsTrue());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kKiosk), IsTrue());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kSmartCard), IsTrue());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(decrypt_allowed(AuthFactorType::kUnspecified), IsFalse());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kUnspecified), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kUnspecified), IsFalse());
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::IsLightAuthSupported. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, IsLightAuthSupported) {
  auto decrypt_allowed = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsLightAuthSupported(AuthIntent::kDecrypt);
  };
  auto vonly_allowed = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsLightAuthSupported(
        AuthIntent::kVerifyOnly);
  };
  auto webauthn_allowed = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsLightAuthSupported(AuthIntent::kWebAuthn);
  };

  EXPECT_THAT(decrypt_allowed(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kPin), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(decrypt_allowed(AuthFactorType::kFingerprint), IsFalse());

  EXPECT_THAT(vonly_allowed(AuthFactorType::kPassword), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kPin), IsFalse());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kSmartCard), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kLegacyFingerprint), IsTrue());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kFingerprint), IsFalse());

  EXPECT_THAT(webauthn_allowed(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kPin), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kLegacyFingerprint), IsTrue());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kFingerprint), IsFalse());

  EXPECT_THAT(decrypt_allowed(AuthFactorType::kUnspecified), IsFalse());
  EXPECT_THAT(vonly_allowed(AuthFactorType::kUnspecified), IsFalse());
  EXPECT_THAT(webauthn_allowed(AuthFactorType::kUnspecified), IsFalse());
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::IsFullAuthRepeatable. We do this here
// instead of in a per-driver test because the check is trivial enough that one
// test is simpler to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, IsFullAuthRepeatable) {
  auto is_repeatable = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsFullAuthRepeatable();
  };

  EXPECT_THAT(is_repeatable(AuthFactorType::kPassword), IsTrue());
  EXPECT_THAT(is_repeatable(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(is_repeatable(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(is_repeatable(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(is_repeatable(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(is_repeatable(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(is_repeatable(AuthFactorType::kFingerprint), IsFalse());
  EXPECT_THAT(is_repeatable(AuthFactorType::kUnspecified), IsFalse());
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::GetIntentConfigurability. We do this here instead of
// in a per-driver test because the check is trivial enough that one test is
// simpler to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, GetIntentConfigurability) {
  // Helpers for wrapping the different call parameters for
  // GetIntentConfigurability and for the Eq matchers. This makes the
  // EXPECT_THAT calls a little more table-like and easier to read.
  auto decrypt = [this](AuthFactorType type) {
    return manager_.GetDriver(type).GetIntentConfigurability(
        AuthIntent::kDecrypt);
  };
  auto vonly = [this](AuthFactorType type) {
    return manager_.GetDriver(type).GetIntentConfigurability(
        AuthIntent::kVerifyOnly);
  };
  auto webauthn = [this](AuthFactorType type) {
    return manager_.GetDriver(type).GetIntentConfigurability(
        AuthIntent::kWebAuthn);
  };
  auto IsNotConfigurable = []() {
    return Eq(AuthFactorDriver::IntentConfigurability::kNotConfigurable);
  };
  auto IsEnabledByDefault = []() {
    return Eq(AuthFactorDriver::IntentConfigurability::kEnabledByDefault);
  };
  auto IsDisabledByDefault = []() {
    return Eq(AuthFactorDriver::IntentConfigurability::kDisabledByDefault);
  };

  EXPECT_THAT(decrypt(AuthFactorType::kPassword), IsNotConfigurable());
  EXPECT_THAT(decrypt(AuthFactorType::kPin), IsNotConfigurable());
  EXPECT_THAT(decrypt(AuthFactorType::kCryptohomeRecovery),
              IsNotConfigurable());
  EXPECT_THAT(decrypt(AuthFactorType::kKiosk), IsNotConfigurable());
  EXPECT_THAT(decrypt(AuthFactorType::kSmartCard), IsNotConfigurable());
  EXPECT_THAT(decrypt(AuthFactorType::kLegacyFingerprint), IsNotConfigurable());
  EXPECT_THAT(decrypt(AuthFactorType::kFingerprint), IsDisabledByDefault());

  EXPECT_THAT(vonly(AuthFactorType::kPassword), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kPin), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kCryptohomeRecovery), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kKiosk), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kSmartCard), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kLegacyFingerprint), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kFingerprint), IsEnabledByDefault());

  EXPECT_THAT(webauthn(AuthFactorType::kPassword), IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kPin), IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kCryptohomeRecovery),
              IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kKiosk), IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kSmartCard), IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kLegacyFingerprint),
              IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kFingerprint), IsNotConfigurable());

  EXPECT_THAT(decrypt(AuthFactorType::kUnspecified), IsNotConfigurable());
  EXPECT_THAT(vonly(AuthFactorType::kUnspecified), IsNotConfigurable());
  EXPECT_THAT(webauthn(AuthFactorType::kUnspecified), IsNotConfigurable());
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::NeedsResetSecret. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, NeedsResetSecret) {
  auto needs_secret = [this](AuthFactorType type) {
    return manager_.GetDriver(type).NeedsResetSecret();
  };

  EXPECT_THAT(needs_secret(AuthFactorType::kPassword), IsTrue());
  EXPECT_THAT(needs_secret(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(needs_secret(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kKiosk), IsTrue());
  EXPECT_THAT(needs_secret(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(needs_secret(AuthFactorType::kFingerprint), IsFalse());

  EXPECT_THAT(needs_secret(AuthFactorType::kUnspecified), IsFalse());
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::NeedsRateLimiter. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, NeedsRateLimiter) {
  auto needs_limiter = [this](AuthFactorType type) {
    return manager_.GetDriver(type).NeedsRateLimiter();
  };

  EXPECT_THAT(needs_limiter(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kPin), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(needs_limiter(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(needs_limiter(AuthFactorType::kUnspecified), IsFalse());

  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::IsDelaySupported. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, IsDelaySupported) {
  auto is_delayable = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsDelaySupported();
  };

  EXPECT_THAT(is_delayable(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(is_delayable(AuthFactorType::kPin), IsTrue());
  EXPECT_THAT(is_delayable(AuthFactorType::kCryptohomeRecovery), IsTrue());
  EXPECT_THAT(is_delayable(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(is_delayable(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(is_delayable(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(is_delayable(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(is_delayable(AuthFactorType::kUnspecified), IsFalse());

  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::IsExpirationSupported. We do this here instead of in a
// per-driver test because the check is trivial enough that one test is simpler
// to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, IsExpirationSupported) {
  auto has_expiration = [this](AuthFactorType type) {
    return manager_.GetDriver(type).IsExpirationSupported();
  };

  EXPECT_THAT(has_expiration(AuthFactorType::kPassword), IsFalse());
  EXPECT_THAT(has_expiration(AuthFactorType::kPin), IsFalse());
  EXPECT_THAT(has_expiration(AuthFactorType::kCryptohomeRecovery), IsFalse());
  EXPECT_THAT(has_expiration(AuthFactorType::kKiosk), IsFalse());
  EXPECT_THAT(has_expiration(AuthFactorType::kSmartCard), IsFalse());
  EXPECT_THAT(has_expiration(AuthFactorType::kLegacyFingerprint), IsFalse());
  EXPECT_THAT(has_expiration(AuthFactorType::kFingerprint), IsTrue());

  EXPECT_THAT(has_expiration(AuthFactorType::kUnspecified), IsFalse());

  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::GetAuthFactorLabelArity. We do this here instead of in
// a per-driver test because the check is trivial enough that one test is
// simpler to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, GetAuthFactorLabelArity) {
  auto get_arity = [this](AuthFactorType type) {
    return manager_.GetDriver(type).GetAuthFactorLabelArity();
  };

  EXPECT_THAT(get_arity(AuthFactorType::kPassword),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kPin),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kCryptohomeRecovery),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kKiosk),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kSmartCard),
              Eq(AuthFactorLabelArity::kSingle));
  EXPECT_THAT(get_arity(AuthFactorType::kLegacyFingerprint),
              Eq(AuthFactorLabelArity::kNone));
  EXPECT_THAT(get_arity(AuthFactorType::kFingerprint),
              Eq(AuthFactorLabelArity::kMultiple));

  EXPECT_THAT(get_arity(AuthFactorType::kUnspecified),
              Eq(AuthFactorLabelArity::kNone));
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

// Test AuthFactorDriver::GetKnowledgeFactorType. We do this here
// instead of in a per-driver test because the check is trivial enough that one
// test is simpler to validate than N separate tests.
TEST_F(AuthFactorDriverManagerTest, GetKnowledgeFactorType) {
  auto knowledge_factor_type = [this](AuthFactorType type) {
    return manager_.GetDriver(type).GetKnowledgeFactorType();
  };

  EXPECT_THAT(knowledge_factor_type(AuthFactorType::kPassword),
              Optional(KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PASSWORD));
  EXPECT_THAT(knowledge_factor_type(AuthFactorType::kPin),
              Optional(KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN));
  EXPECT_FALSE(
      knowledge_factor_type(AuthFactorType::kCryptohomeRecovery).has_value());
  EXPECT_FALSE(knowledge_factor_type(AuthFactorType::kKiosk).has_value());
  EXPECT_FALSE(knowledge_factor_type(AuthFactorType::kSmartCard).has_value());
  EXPECT_FALSE(
      knowledge_factor_type(AuthFactorType::kLegacyFingerprint).has_value());
  EXPECT_FALSE(knowledge_factor_type(AuthFactorType::kFingerprint).has_value());

  EXPECT_FALSE(knowledge_factor_type(AuthFactorType::kUnspecified).has_value());

  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

}  // namespace
}  // namespace cryptohome

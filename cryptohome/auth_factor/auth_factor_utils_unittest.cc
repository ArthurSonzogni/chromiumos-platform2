// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <absl/types/variant.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "UserDataAuth.pb.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/mock_platform.h"

namespace cryptohome {

namespace {

using ::brillo::SecureBlob;
using ::hwsec_foundation::error::testing::IsOk;
using ::testing::_;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;

constexpr char kLabel[] = "some-label";
constexpr char kPinLabel[] = "some-pin-label";
constexpr char kObfuscatedUsername[] = "obfuscated";

std::unique_ptr<AuthFactor> CreatePasswordAuthFactor() {
  AuthFactorMetadata metadata = {.metadata = PasswordAuthFactorMetadata()};
  return std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, kLabel, metadata,
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = SecureBlob("fake salt"),
              .tpm_key = SecureBlob("fake tpm key"),
              .extended_tpm_key = SecureBlob("fake extended tpm key"),
              .tpm_public_key_hash = SecureBlob("fake tpm public key hash"),
          }});
}

std::unique_ptr<AuthFactor> CreatePinAuthFactor() {
  AuthFactorMetadata metadata = {.metadata = PinAuthFactorMetadata()};
  return std::make_unique<AuthFactor>(
      AuthFactorType::kPin, kPinLabel, metadata,
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = SecureBlob("fake salt"),
                         .chaps_iv = SecureBlob("fake chaps IV"),
                         .fek_iv = SecureBlob("fake file encryption IV"),
                         .reset_salt = SecureBlob("more fake salt"),
                     }});
}

}  // namespace

TEST(AuthFactorUtilsTest, AuthFactorTypeConversionIsInvertable) {
  // Test a round trip of conversion gets back the original types.
  EXPECT_EQ(
      AuthFactorTypeFromProto(AuthFactorTypeToProto(AuthFactorType::kPassword)),
      AuthFactorType::kPassword);
  EXPECT_EQ(
      AuthFactorTypeFromProto(AuthFactorTypeToProto(AuthFactorType::kPin)),
      AuthFactorType::kPin);
  EXPECT_EQ(AuthFactorTypeFromProto(
                AuthFactorTypeToProto(AuthFactorType::kCryptohomeRecovery)),
            AuthFactorType::kCryptohomeRecovery);
  EXPECT_EQ(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                user_data_auth::AUTH_FACTOR_TYPE_PASSWORD)),
            user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_EQ(AuthFactorTypeToProto(
                *AuthFactorTypeFromProto(user_data_auth::AUTH_FACTOR_TYPE_PIN)),
            user_data_auth::AUTH_FACTOR_TYPE_PIN);
  EXPECT_EQ(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY)),
            user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  EXPECT_EQ(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                user_data_auth::AUTH_FACTOR_TYPE_KIOSK)),
            user_data_auth::AUTH_FACTOR_TYPE_KIOSK);
  EXPECT_EQ(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD)),
            user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD);

  // These proto types are known to not be supported
  EXPECT_EQ(
      AuthFactorTypeFromProto(user_data_auth::AUTH_FACTOR_TYPE_UNSPECIFIED),
      AuthFactorType::kUnspecified);
}

TEST(AuthFactorUtilsTest, AuthFactorTypeConversionFromProtoCoversAllValues) {
  // With proto enums we can't use a "complete" switch to cover every value so
  // we enfore that every value is given an explicit mapping (even if just to
  // Unspecified) via this test.
  for (int raw_type = user_data_auth::AuthFactorType_MIN;
       raw_type <= user_data_auth::AuthFactorType_MAX; ++raw_type) {
    if (!user_data_auth::AuthFactorType_IsValid(raw_type)) {
      continue;
    }
    auto type = static_cast<user_data_auth::AuthFactorType>(raw_type);
    EXPECT_NE(AuthFactorTypeFromProto(type), std::nullopt)
        << "user_data_auth::AuthFactorType has no mapping for "
        << user_data_auth::AuthFactorType_Name(type);
  }
}

TEST(AuthFactorUtilsTest, AuthFactorMetaDataCheck) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auth_factor_proto.mutable_password_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorMetadata auth_factor_metadata;
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  EXPECT_TRUE(GetAuthFactorMetadata(auth_factor_proto, auth_factor_metadata,
                                    auth_factor_type, auth_factor_label));

  // Verify
  EXPECT_TRUE(absl::holds_alternative<PasswordAuthFactorMetadata>(
      auth_factor_metadata.metadata));
  EXPECT_EQ(auth_factor_type, AuthFactorType::kPassword);
  EXPECT_EQ(auth_factor_label, kLabel);
}

// Test `GetAuthFactorProto()` for a password auth factor.
TEST(AuthFactorUtilsTest, GetProtoPassword) {
  // Setup
  AuthFactorMetadata metadata = {.metadata = PasswordAuthFactorMetadata()};

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kPassword, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(), user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_EQ(proto.value().label(), kLabel);
  ASSERT_TRUE(proto.value().has_password_metadata());
}

// Test `GetAuthFactorProto()` fails when the password metadata is missing.
TEST(AuthFactorUtilsTest, GetProtoPasswordErrorNoMetadata) {
  // Setup
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kPassword, kLabel);

  // Verify
  EXPECT_FALSE(proto.has_value());
}

// Test `LoadUserAuthFactorProtos()` with no auth factors available.
TEST(AuthFactorUtilsTest, LoadUserAuthFactorProtosNoFactors) {
  // Setup
  NiceMock<MockPlatform> platform;
  AuthFactorManager manager(&platform);
  google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>
      protos;

  // Test
  LoadUserAuthFactorProtos(&manager, kObfuscatedUsername, &protos);

  // Verify
  EXPECT_THAT(protos, IsEmpty());
}

// Test `LoadUserAuthFactorProtos()` with an some auth factors available.
TEST(AuthFactorUtilsTest, LoadUserAuthFactorProtosWithFactors) {
  // Setup
  NiceMock<MockPlatform> platform;
  AuthFactorManager manager(&platform);
  auto factor1 = CreatePasswordAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor1), IsOk());
  auto factor2 = CreatePinAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor2), IsOk());
  google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>
      protos;

  // Test
  LoadUserAuthFactorProtos(&manager, kObfuscatedUsername, &protos);

  // Sort the protos by label. This is done to produce a consistent ordering
  // which makes it easier to verify the results.
  std::sort(protos.pointer_begin(), protos.pointer_end(),
            [](const user_data_auth::AuthFactorWithStatus* lhs,
               const user_data_auth::AuthFactorWithStatus* rhs) {
              return lhs->auth_factor().label() < rhs->auth_factor().label();
            });

  // Verify
  ASSERT_EQ(protos.size(), 2);
  EXPECT_EQ(protos[0].auth_factor().label(), kLabel);
  EXPECT_TRUE(protos[0].auth_factor().has_password_metadata());
  EXPECT_EQ(protos[1].auth_factor().label(), kPinLabel);
  EXPECT_TRUE(protos[1].auth_factor().has_pin_metadata());
}

// Test `LoadUserAuthFactorProtos()` with some auth factors that we can't read.
TEST(AuthFactorUtilsTest, LoadUserAuthFactorProtosWithUnreadableFactors) {
  // Setup
  NiceMock<MockPlatform> platform;
  AuthFactorManager manager(&platform);
  auto factor1 = CreatePasswordAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor1), IsOk());
  auto factor2 = CreatePinAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor2), IsOk());
  google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>
      protos;
  // Make all file reads fail now, so that we can't read the auth factors.
  EXPECT_CALL(platform, ReadFile(_, _)).WillRepeatedly(Return(false));

  // Test
  LoadUserAuthFactorProtos(&manager, kObfuscatedUsername, &protos);

  // Verify
  EXPECT_THAT(protos, IsEmpty());
}

// Test `GetAuthFactorProto()` for a pin auth factor.
TEST(AuthFactorUtilsTest, GetProtoPin) {
  // Setup
  AuthFactorMetadata metadata = {.metadata = PinAuthFactorMetadata()};

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kPin, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(), user_data_auth::AUTH_FACTOR_TYPE_PIN);
  EXPECT_EQ(proto.value().label(), kLabel);
  ASSERT_TRUE(proto.value().has_pin_metadata());
}

// Test `GetAuthFactorProto()` for a kiosk auth factor.
TEST(AuthFactorUtilsTest, GetProtoKiosk) {
  // Setup
  AuthFactorMetadata metadata = {.metadata = KioskAuthFactorMetadata()};

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kKiosk, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(), user_data_auth::AUTH_FACTOR_TYPE_KIOSK);
  EXPECT_EQ(proto.value().label(), kLabel);
  ASSERT_TRUE(proto.value().has_kiosk_metadata());
}

// Test `GetAuthFactorProto()` for a recovery auth factor.
TEST(AuthFactorUtilsTest, GetProtoRecovery) {
  // Setup
  AuthFactorMetadata metadata = {.metadata =
                                     CryptohomeRecoveryAuthFactorMetadata()};

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kCryptohomeRecovery, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(),
            user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  EXPECT_EQ(proto.value().label(), kLabel);
  ASSERT_TRUE(proto.value().has_cryptohome_recovery_metadata());
}

// Test `GetAuthFactorProto()` for  when pin auth factor does not have metadata.
TEST(AuthFactorUtilsTest, GetProtoPinNullOpt) {
  // Setup
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kPin, kLabel);

  // Verify
  ASSERT_FALSE(proto.has_value());
}

// Test `GetAuthFactorProto()` for  when kiosk auth factor does not have
// metadata.
TEST(AuthFactorUtilsTest, GetProtoKioskNullOpt) {
  // Setup
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kKiosk, kLabel);

  // Verify
  ASSERT_FALSE(proto.has_value());
}

// Test `GetAuthFactorProto()` for  when recovery auth factor does not have
// metadata.
TEST(AuthFactorUtilsTest, GetProtoRecoveryNullOpt) {
  // Setup
  AuthFactorMetadata metadata;
  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kCryptohomeRecovery, kLabel);

  // Verify
  ASSERT_FALSE(proto.has_value());
}

// Test `NeedsResetSecret()` to return correct value.
TEST(AuthFactorUtilsTest, NeedsResetSecret) {
  EXPECT_TRUE(NeedsResetSecret(AuthFactorType::kPin));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kPassword));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kKiosk));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kCryptohomeRecovery));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kSmartCard));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kUnspecified));
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 5,
                "All types of AuthFactorType are not all included here");
}

}  // namespace cryptohome

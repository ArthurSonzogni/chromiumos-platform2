// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/protobuf.h"

#include <base/test/scoped_chromeos_version_info.h>
#include <base/time/time.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_factor/type.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/util/proto_enum.h"

namespace cryptohome {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::VariantWith;

constexpr char kLabel[] = "some-label";
constexpr char kChromeosVersion[] = "1.2.3_a_b_c";
constexpr char kChromeVersion[] = "1.2.3.4";

TEST(AuthFactorTypeToProto, ConversionIsInvertable) {
  // Test a round trip of conversion gets back the original types.
  EXPECT_THAT(
      AuthFactorTypeFromProto(AuthFactorTypeToProto(AuthFactorType::kPassword)),
      Eq(AuthFactorType::kPassword));
  EXPECT_THAT(
      AuthFactorTypeFromProto(AuthFactorTypeToProto(AuthFactorType::kPin)),
      Eq(AuthFactorType::kPin));
  EXPECT_THAT(AuthFactorTypeFromProto(
                  AuthFactorTypeToProto(AuthFactorType::kCryptohomeRecovery)),
              Eq(AuthFactorType::kCryptohomeRecovery));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_PASSWORD)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_PIN)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_PIN));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_KIOSK)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_KIOSK));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT));
  EXPECT_THAT(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                  user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT)),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT));

  // These proto types are known to not be supported
  EXPECT_THAT(
      AuthFactorTypeFromProto(user_data_auth::AUTH_FACTOR_TYPE_UNSPECIFIED),
      Eq(AuthFactorType::kUnspecified));
}

TEST(AuthFactorTypeFromProto, ConversionFromProtoCoversAllValues) {
  // With proto enums we can't use a "complete" switch to cover every value so
  // we enforce that every value is given an explicit mapping (even if just to
  // Unspecified) via this test.
  for (auto type : PROTOBUF_ENUM_ALL_VALUES(user_data_auth::AuthFactorType)) {
    EXPECT_THAT(AuthFactorTypeFromProto(type), Optional(_))
        << "user_data_auth::AuthFactorType has no mapping for "
        << user_data_auth::AuthFactorType_Name(type);
  }
}

TEST(AuthFactorPreparePurposeFromProto, ConversionFromProtoCoversAllValues) {
  // With proto enums we can't use a "complete" switch to cover every value so
  // we enforce that every value other than unspecified is given an explicit
  // mapping.
  for (auto purpose :
       PROTOBUF_ENUM_ALL_VALUES(user_data_auth::AuthFactorPreparePurpose)) {
    if (purpose == user_data_auth::PURPOSE_UNSPECIFIED) {
      EXPECT_THAT(AuthFactorPreparePurposeFromProto(purpose), Eq(std::nullopt));
    } else {
      EXPECT_THAT(AuthFactorPreparePurposeFromProto(purpose), Optional(_))
          << "user_data_auth::AuthFactorPreparePurpose has no mapping for "
          << user_data_auth::AuthFactorPreparePurpose_Name(purpose);
    }
  }
}

TEST(PopulateSysinfoWithOsVersionTest, Success) {
  static constexpr char kLsbRelease[] =
      R"(CHROMEOS_RELEASE_NAME=Chrome OS
CHROMEOS_RELEASE_VERSION=11012.0.2018_08_28_1422
)";
  base::test::ScopedChromeOSVersionInfo scoped_version(
      kLsbRelease, /*lsb_release_time=*/base::Time());

  static constexpr char kLsbReleaseVersion[] = "11012.0.2018_08_28_1422";
  static constexpr char kOtherVersion[] = "11011.0.2017_07_27_1421";

  // Try filling in a blank proto.
  user_data_auth::AuthFactor auth_factor;
  PopulateAuthFactorProtoWithSysinfo(auth_factor);
  EXPECT_THAT(auth_factor.common_metadata().chromeos_version_last_updated(),
              Eq(kLsbReleaseVersion));

  // Try filling in a proto with existing data.
  user_data_auth::AuthFactor auth_factor_with_existing_data;
  auth_factor_with_existing_data.mutable_common_metadata()
      ->set_chromeos_version_last_updated(kOtherVersion);
  EXPECT_THAT(auth_factor_with_existing_data.common_metadata()
                  .chromeos_version_last_updated(),
              Eq(kOtherVersion));
  PopulateAuthFactorProtoWithSysinfo(auth_factor_with_existing_data);
  EXPECT_THAT(auth_factor_with_existing_data.common_metadata()
                  .chromeos_version_last_updated(),
              Eq(kLsbReleaseVersion));
}

TEST(PopulateSysinfoWithOsVersionTest, Failure) {
  static constexpr char kLsbRelease[] =
      R"(CHROMEOS_RELEASE_NAME=Chrome OS
)";
  base::test::ScopedChromeOSVersionInfo scoped_version(
      kLsbRelease, /*lsb_release_time=*/base::Time());

  static constexpr char kVersion[] = "11011.0.2017_07_27_1421";

  // Try filling in a blank proto.
  user_data_auth::AuthFactor auth_factor;
  PopulateAuthFactorProtoWithSysinfo(auth_factor);
  EXPECT_THAT(auth_factor.common_metadata().chromeos_version_last_updated(),
              IsEmpty());

  // Try filling in a proto with existing data.
  user_data_auth::AuthFactor auth_factor_with_existing_data;
  auth_factor_with_existing_data.mutable_common_metadata()
      ->set_chromeos_version_last_updated(kVersion);
  EXPECT_THAT(auth_factor_with_existing_data.common_metadata()
                  .chromeos_version_last_updated(),
              Eq(kVersion));
  PopulateAuthFactorProtoWithSysinfo(auth_factor_with_existing_data);
  EXPECT_THAT(auth_factor_with_existing_data.common_metadata()
                  .chromeos_version_last_updated(),
              IsEmpty());
}

TEST(AuthFactorPropertiesFromProtoTest, AuthFactorMetaDataCheck) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  auth_factor_proto.mutable_password_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsTrue());

  // Verify
  EXPECT_THAT(auth_factor_metadata.common.chromeos_version_last_updated,
              Eq(kChromeosVersion));
  EXPECT_THAT(auth_factor_metadata.common.chrome_version_last_updated,
              Eq(kChromeVersion));
  const auto* password_metadata =
      std::get_if<PasswordMetadata>(&auth_factor_metadata.metadata);
  ASSERT_NE(password_metadata, nullptr);
  EXPECT_FALSE(password_metadata->hash_info.has_value());
  EXPECT_THAT(auth_factor_type, Eq(AuthFactorType::kPassword));
  EXPECT_THAT(auth_factor_label, Eq(kLabel));
}

TEST(AuthFactorPropertiesFromProtoTest,
     AuthFactorMetaDataCheckKnowledgeFactorHashInfo) {
  const std::string kSalt(16, 0xAA);
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  user_data_auth::KnowledgeFactorHashInfo hash_info;
  hash_info.set_algorithm(
      LockScreenKnowledgeFactorHashAlgorithm::HASH_TYPE_SHA256_TOP_HALF);
  hash_info.set_salt(kSalt);
  *auth_factor_proto.mutable_password_metadata()->mutable_hash_info() =
      hash_info;
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsTrue());

  // Verify
  EXPECT_THAT(auth_factor_metadata.common.chromeos_version_last_updated,
              Eq(kChromeosVersion));
  EXPECT_THAT(auth_factor_metadata.common.chrome_version_last_updated,
              Eq(kChromeVersion));
  EXPECT_THAT(auth_factor_metadata.common.lockout_policy,
              Optional(SerializedLockoutPolicy::NO_LOCKOUT));
  const auto* password_metadata =
      std::get_if<PasswordMetadata>(&auth_factor_metadata.metadata);
  ASSERT_NE(password_metadata, nullptr);
  ASSERT_TRUE(password_metadata->hash_info.has_value());
  EXPECT_THAT(
      password_metadata->hash_info->algorithm,
      Optional(
          SerializedLockScreenKnowledgeFactorHashAlgorithm::SHA256_TOP_HALF));
  EXPECT_EQ(password_metadata->hash_info->salt, brillo::BlobFromString(kSalt));
  EXPECT_THAT(auth_factor_type, Eq(AuthFactorType::kPassword));
  EXPECT_THAT(auth_factor_label, Eq(kLabel));
}

TEST(AuthFactorPropertiesFromProtoTest, AuthFactorMetaDataCheckPin) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  common_metadata_proto.set_lockout_policy(
      user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED);
  auth_factor_proto.mutable_pin_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsTrue());

  // Verify
  EXPECT_THAT(auth_factor_metadata.common.chromeos_version_last_updated,
              Eq(kChromeosVersion));
  EXPECT_THAT(auth_factor_metadata.common.chrome_version_last_updated,
              Eq(kChromeVersion));
  EXPECT_THAT(auth_factor_metadata.common.lockout_policy,
              Optional(SerializedLockoutPolicy::ATTEMPT_LIMITED));
  EXPECT_THAT(auth_factor_metadata.metadata, VariantWith<PinMetadata>(_));
  EXPECT_THAT(auth_factor_type, Eq(AuthFactorType::kPin));
  EXPECT_THAT(auth_factor_label, Eq(kLabel));
}

TEST(AuthFactorPropertiesFromProtoTest, AuthFactorMetaDataCheckPinTimeLimit) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  common_metadata_proto.set_lockout_policy(
      user_data_auth::LOCKOUT_POLICY_TIME_LIMITED);
  auth_factor_proto.mutable_pin_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsTrue());

  // Verify
  EXPECT_THAT(auth_factor_metadata.common.chromeos_version_last_updated,
              Eq(kChromeosVersion));
  EXPECT_THAT(auth_factor_metadata.common.chrome_version_last_updated,
              Eq(kChromeVersion));
  EXPECT_THAT(auth_factor_metadata.common.lockout_policy,
              Optional(SerializedLockoutPolicy::TIME_LIMITED));
  EXPECT_THAT(auth_factor_metadata.metadata, VariantWith<PinMetadata>(_));
  EXPECT_THAT(auth_factor_type, Eq(AuthFactorType::kPin));
  EXPECT_THAT(auth_factor_label, Eq(kLabel));
}

TEST(AuthFactorPropertiesFromProtoTest,
     AuthFactorMetaDataCheckPinAttemptLimitFeaturesNull) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  common_metadata_proto.set_lockout_policy(
      user_data_auth::LOCKOUT_POLICY_TIME_LIMITED);
  auth_factor_proto.mutable_pin_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsTrue());

  // Verify
  EXPECT_THAT(auth_factor_metadata.common.chromeos_version_last_updated,
              Eq(kChromeosVersion));
  EXPECT_THAT(auth_factor_metadata.common.chrome_version_last_updated,
              Eq(kChromeVersion));
  EXPECT_THAT(auth_factor_metadata.common.lockout_policy,
              Optional(SerializedLockoutPolicy::TIME_LIMITED));
  EXPECT_THAT(auth_factor_metadata.metadata, VariantWith<PinMetadata>(_));
  EXPECT_THAT(auth_factor_type, Eq(AuthFactorType::kPin));
  EXPECT_THAT(auth_factor_label, Eq(kLabel));
}

TEST(AuthFactorPropertiesFromProtoTest,
     AuthFactorMetaDataCheckPinAttemptLimitFeatureEnabled) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  common_metadata_proto.set_lockout_policy(
      user_data_auth::LOCKOUT_POLICY_TIME_LIMITED);
  auth_factor_proto.mutable_pin_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  features.SetDefaultForFeature(Features::kModernPin, /*enabled=*/true);
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsTrue());

  // Verify
  EXPECT_THAT(auth_factor_metadata.common.chromeos_version_last_updated,
              Eq(kChromeosVersion));
  EXPECT_THAT(auth_factor_metadata.common.chrome_version_last_updated,
              Eq(kChromeVersion));
  EXPECT_THAT(auth_factor_metadata.common.lockout_policy,
              Optional(SerializedLockoutPolicy::TIME_LIMITED));
  EXPECT_THAT(auth_factor_metadata.metadata, VariantWith<PinMetadata>(_));
  EXPECT_THAT(auth_factor_type, Eq(AuthFactorType::kPin));
  EXPECT_THAT(auth_factor_label, Eq(kLabel));
}

TEST(AuthFactorPropertiesFromProtoTest,
     AuthFactorMetaDataCheckPinAttemptLimitFeatureEnabledWrongInput) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
  common_metadata_proto.set_lockout_policy(
      user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED);
  auth_factor_proto.mutable_pin_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  AuthFactorMetadata auth_factor_metadata;
  FakeFeaturesForTesting features;
  features.SetDefaultForFeature(Features::kModernPin, /*enabled=*/true);
  EXPECT_THAT(AuthFactorPropertiesFromProto(auth_factor_proto, features.async,
                                            auth_factor_type, auth_factor_label,
                                            auth_factor_metadata),
              IsFalse());
}

}  // namespace
}  // namespace cryptohome

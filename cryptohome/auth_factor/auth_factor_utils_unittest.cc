// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <absl/types/variant.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>

#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_prepare_purpose.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/user_secret_stash.h"

namespace cryptohome {
namespace {

using ::brillo::SecureBlob;
using ::brillo::cryptohome::home::SanitizeUserName;
using ::hwsec_foundation::error::testing::IsOk;
using ::testing::_;
using ::testing::DoAll;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

constexpr char kLabel[] = "some-label";
constexpr char kPinLabel[] = "some-pin-label";
constexpr char kObfuscatedUsername[] = "obfuscated";
constexpr char kChromeosVersion[] = "1.2.3_a_b_c";
constexpr char kChromeVersion[] = "1.2.3.4";

// A matcher for an AuthFactorMap element. This will check the type, label and
// storage type of the item. You generally want to combine this with
// UnorderedElementsAre to compare it against an entire AuthFactorMap, but you
// can also use it directly with individual elements in the map.
class AuthFactorMapItemMatcher
    : public ::testing::MatcherInterface<AuthFactorMap::ValueView> {
 public:
  AuthFactorMapItemMatcher(AuthFactorType type,
                           std::string label,
                           AuthFactorStorageType storage_type)
      : type_(type), label_(std::move(label)), storage_type_(storage_type) {}

  bool MatchAndExplain(
      AuthFactorMap::ValueView value,
      ::testing::MatchResultListener* listener) const override {
    bool matches = true;
    if (value.auth_factor().type() != type_) {
      matches = false;
      *listener << "type is: "
                << AuthFactorTypeToString(value.auth_factor().type()) << "\n";
    }
    if (value.auth_factor().label() != label_) {
      matches = false;
      *listener << "label is: " << value.auth_factor().label() << "\n";
    }
    if (value.storage_type() != storage_type_) {
      matches = false;
      *listener << "label is: "
                << AuthFactorStorageTypeToDebugString(value.storage_type())
                << "\n";
    }
    return matches;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "has type " << AuthFactorTypeToString(type_) << ", label " << label_
        << " and storage type "
        << AuthFactorStorageTypeToDebugString(storage_type_);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not have type " << AuthFactorTypeToString(type_)
        << " or does not have label " << label_
        << " or does not have storage type "
        << AuthFactorStorageTypeToDebugString(storage_type_);
  }

 private:
  AuthFactorType type_;
  std::string label_;
  AuthFactorStorageType storage_type_;
};
::testing::Matcher<AuthFactorMap::ValueView> AuthFactorMapItem(
    AuthFactorType type,
    std::string label,
    AuthFactorStorageType storage_type) {
  return ::testing::MakeMatcher<AuthFactorMap::ValueView>(
      new AuthFactorMapItemMatcher(type, std::move(label), storage_type));
}

// Create a generic metadata with the given factor-specific subtype using
// version information from the test constants above.
template <typename MetadataType>
AuthFactorMetadata CreateMetadataWithType() {
  return {
      .common = {.chromeos_version_last_updated = kChromeosVersion,
                 .chrome_version_last_updated = kChromeVersion},
      .metadata = MetadataType(),
  };
}

std::unique_ptr<AuthFactor> CreatePasswordAuthFactor() {
  return std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, kLabel,
      CreateMetadataWithType<PasswordAuthFactorMetadata>(),
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
  return std::make_unique<AuthFactor>(
      AuthFactorType::kPin, kPinLabel,
      CreateMetadataWithType<PinAuthFactorMetadata>(),
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = SecureBlob("fake salt"),
                         .chaps_iv = SecureBlob("fake chaps IV"),
                         .fek_iv = SecureBlob("fake file encryption IV"),
                         .reset_salt = SecureBlob("more fake salt"),
                     }});
}

std::unique_ptr<VaultKeyset> CreatePasswordVaultKeyset(
    const std::string& label) {
  SerializedVaultKeyset serialized_vk;
  serialized_vk.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                          SerializedVaultKeyset::SCRYPT_DERIVED |
                          SerializedVaultKeyset::PCR_BOUND |
                          SerializedVaultKeyset::ECC);
  serialized_vk.set_password_rounds(1);
  serialized_vk.set_tpm_key("tpm-key");
  serialized_vk.set_extended_tpm_key("tpm-extended-key");
  serialized_vk.set_vkk_iv("iv");
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
  auto vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized_vk);
  return vk;
}

std::unique_ptr<VaultKeyset> CreateBackupVaultKeyset(const std::string& label) {
  auto backup_vk = CreatePasswordVaultKeyset(label);
  backup_vk->set_backup_vk_for_testing(true);
  backup_vk->SetResetSeed(brillo::SecureBlob(32, 'A'));
  backup_vk->SetWrappedResetSeed(brillo::SecureBlob(32, 'B'));
  return backup_vk;
}

std::unique_ptr<VaultKeyset> CreateMigratedVaultKeyset(
    const std::string& label) {
  auto migrated_vk = CreateBackupVaultKeyset(label);
  migrated_vk->set_migrated_vk_for_testing(true);
  return migrated_vk;
}

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
  EXPECT_EQ(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT)),
            user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT);
  EXPECT_EQ(AuthFactorTypeToProto(*AuthFactorTypeFromProto(
                user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT)),
            user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);

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

TEST(AuthFactorUtilsTest, PopulateSysinfoWithOsVersion) {
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
  EXPECT_EQ(auth_factor.common_metadata().chromeos_version_last_updated(),
            kLsbReleaseVersion);

  // Try filling in a proto with existing data.
  user_data_auth::AuthFactor auth_factor_with_existing_data;
  auth_factor_with_existing_data.mutable_common_metadata()
      ->set_chromeos_version_last_updated(kOtherVersion);
  EXPECT_EQ(auth_factor_with_existing_data.common_metadata()
                .chromeos_version_last_updated(),
            kOtherVersion);
  PopulateAuthFactorProtoWithSysinfo(auth_factor_with_existing_data);
  EXPECT_EQ(auth_factor_with_existing_data.common_metadata()
                .chromeos_version_last_updated(),
            kLsbReleaseVersion);
}

TEST(AuthFactorUtilsTest, PopulateSysinfoWithOsVersionFails) {
  static constexpr char kLsbRelease[] =
      R"(CHROMEOS_RELEASE_NAME=Chrome OS
)";
  base::test::ScopedChromeOSVersionInfo scoped_version(
      kLsbRelease, /*lsb_release_time=*/base::Time());

  static constexpr char kVersion[] = "11011.0.2017_07_27_1421";

  // Try filling in a blank proto.
  user_data_auth::AuthFactor auth_factor;
  PopulateAuthFactorProtoWithSysinfo(auth_factor);
  EXPECT_EQ(auth_factor.common_metadata().chromeos_version_last_updated(), "");

  // Try filling in a proto with existing data.
  user_data_auth::AuthFactor auth_factor_with_existing_data;
  auth_factor_with_existing_data.mutable_common_metadata()
      ->set_chromeos_version_last_updated(kVersion);
  EXPECT_EQ(auth_factor_with_existing_data.common_metadata()
                .chromeos_version_last_updated(),
            kVersion);
  PopulateAuthFactorProtoWithSysinfo(auth_factor_with_existing_data);
  EXPECT_EQ(auth_factor_with_existing_data.common_metadata()
                .chromeos_version_last_updated(),
            "");
}

TEST(AuthFactorUtilsTest, AuthFactorMetaDataCheck) {
  // Setup
  user_data_auth::AuthFactor auth_factor_proto;
  auto& common_metadata_proto = *auth_factor_proto.mutable_common_metadata();
  common_metadata_proto.set_chromeos_version_last_updated(kChromeosVersion);
  common_metadata_proto.set_chrome_version_last_updated(kChromeVersion);
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
  EXPECT_EQ(auth_factor_metadata.common.chromeos_version_last_updated,
            kChromeosVersion);
  EXPECT_EQ(auth_factor_metadata.common.chrome_version_last_updated,
            kChromeVersion);
  EXPECT_TRUE(absl::holds_alternative<PasswordAuthFactorMetadata>(
      auth_factor_metadata.metadata));
  EXPECT_EQ(auth_factor_type, AuthFactorType::kPassword);
  EXPECT_EQ(auth_factor_label, kLabel);
}

// Test `GetAuthFactorProto()` for a password auth factor.
TEST(AuthFactorUtilsTest, GetProtoPassword) {
  // Setup
  AuthFactorMetadata metadata =
      CreateMetadataWithType<PasswordAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kPassword, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto->common_metadata().chromeos_version_last_updated(),
            kChromeosVersion);
  EXPECT_EQ(proto->common_metadata().chrome_version_last_updated(),
            kChromeVersion);
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
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>
      protos;

  // Test
  LoadUserAuthFactorProtos(&manager, auth_block_utility_, kObfuscatedUsername,
                           &protos);

  // Verify
  EXPECT_THAT(protos, IsEmpty());
}

// Test `LoadUserAuthFactorProtos()` with an some auth factors available.
TEST(AuthFactorUtilsTest, LoadUserAuthFactorProtosWithFactors) {
  // Setup
  NiceMock<MockPlatform> platform;
  AuthFactorManager manager(&platform);
  NiceMock<MockAuthBlockUtility> auth_block_utility_;

  auto factor1 = CreatePasswordAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor1), IsOk());
  auto factor2 = CreatePinAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor2), IsOk());
  google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>
      protos;

  // Test
  LoadUserAuthFactorProtos(&manager, auth_block_utility_, kObfuscatedUsername,
                           &protos);

  // Sort the protos by label. This is done to produce a consistent ordering
  // which makes it easier to verify the results.
  std::sort(protos.pointer_begin(), protos.pointer_end(),
            [](const user_data_auth::AuthFactorWithStatus* lhs,
               const user_data_auth::AuthFactorWithStatus* rhs) {
              return lhs->auth_factor().label() < rhs->auth_factor().label();
            });

  // Verify
  ASSERT_EQ(protos.size(), 2);
  EXPECT_EQ(
      protos[0].auth_factor().common_metadata().chromeos_version_last_updated(),
      kChromeosVersion);
  EXPECT_EQ(
      protos[1].auth_factor().common_metadata().chrome_version_last_updated(),
      kChromeVersion);
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
  NiceMock<MockAuthBlockUtility> auth_block_utility_;

  auto factor1 = CreatePasswordAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor1), IsOk());
  auto factor2 = CreatePinAuthFactor();
  ASSERT_THAT(manager.SaveAuthFactor(kObfuscatedUsername, *factor2), IsOk());
  google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>
      protos;
  // Make all file reads fail now, so that we can't read the auth factors.
  EXPECT_CALL(platform, ReadFile(_, _)).WillRepeatedly(Return(false));

  // Test
  LoadUserAuthFactorProtos(&manager, auth_block_utility_, kObfuscatedUsername,
                           &protos);

  // Verify
  EXPECT_THAT(protos, IsEmpty());
}

// Test `GetAuthFactorProto()` for a pin auth factor.
TEST(AuthFactorUtilsTest, GetProtoPin) {
  // Setup
  AuthFactorMetadata metadata = CreateMetadataWithType<PinAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kPin, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(), user_data_auth::AUTH_FACTOR_TYPE_PIN);
  EXPECT_EQ(proto.value().label(), kLabel);
  EXPECT_EQ(proto->common_metadata().chromeos_version_last_updated(),
            kChromeosVersion);
  EXPECT_EQ(proto->common_metadata().chrome_version_last_updated(),
            kChromeVersion);
  ASSERT_TRUE(proto.value().has_pin_metadata());
}

// Test `GetAuthFactorProto()` for a kiosk auth factor.
TEST(AuthFactorUtilsTest, GetProtoKiosk) {
  // Setup
  AuthFactorMetadata metadata =
      CreateMetadataWithType<KioskAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kKiosk, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(), user_data_auth::AUTH_FACTOR_TYPE_KIOSK);
  EXPECT_EQ(proto.value().label(), kLabel);
  EXPECT_EQ(proto->common_metadata().chromeos_version_last_updated(),
            kChromeosVersion);
  EXPECT_EQ(proto->common_metadata().chrome_version_last_updated(),
            kChromeVersion);
  ASSERT_TRUE(proto.value().has_kiosk_metadata());
}

// Test `GetAuthFactorProto()` for a recovery auth factor.
TEST(AuthFactorUtilsTest, GetProtoRecovery) {
  // Setup
  AuthFactorMetadata metadata =
      CreateMetadataWithType<CryptohomeRecoveryAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      GetAuthFactorProto(metadata, AuthFactorType::kCryptohomeRecovery, kLabel);

  // Verify
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto.value().type(),
            user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  EXPECT_EQ(proto.value().label(), kLabel);
  EXPECT_EQ(proto->common_metadata().chromeos_version_last_updated(),
            kChromeosVersion);
  EXPECT_EQ(proto->common_metadata().chrome_version_last_updated(),
            kChromeVersion);
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
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kLegacyFingerprint));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kFingerprint));
  EXPECT_FALSE(NeedsResetSecret(AuthFactorType::kUnspecified));
  static_assert(static_cast<int>(AuthFactorType::kUnspecified) == 7,
                "All types of AuthFactorType are not all included here");
}

TEST(AuthSessionProtoUtils, AuthFactorPreparePurposeFromProto) {
  EXPECT_EQ(AuthFactorPreparePurposeFromProto(
                user_data_auth::PURPOSE_ADD_AUTH_FACTOR),
            AuthFactorPreparePurpose::kPrepareAddAuthFactor);
  EXPECT_EQ(AuthFactorPreparePurposeFromProto(
                user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR),
            AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor);
  EXPECT_EQ(
      AuthFactorPreparePurposeFromProto(user_data_auth::PURPOSE_UNSPECIFIED),
      std::nullopt);
}

class LoadAuthFactorMapTest : public ::testing::Test {
 public:
  LoadAuthFactorMapTest() : kObfuscatedUsername(SanitizeUserName(kUsername)) {}

 protected:
  // Install mocks to set up vault keysets for testing. Expects a map of VK
  // labels to factory functions that will construct a VaultKeyset object.
  void InstallVaultKeysets(
      std::map<std::string,
               std::unique_ptr<VaultKeyset> (*)(const std::string&)>
          vk_factory_map) {
    std::vector<int> key_indicies;
    for (const auto& [label, factory] : vk_factory_map) {
      int index = key_indicies.size();
      key_indicies.push_back(index);
      EXPECT_CALL(keyset_management_,
                  LoadVaultKeysetForUser(kObfuscatedUsername, index))
          .WillRepeatedly([label = label, factory = factory](auto...) {
            return factory(label);
          });
    }
    EXPECT_CALL(keyset_management_, GetVaultKeysets(kObfuscatedUsername, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(key_indicies), Return(true)));
  }

  // Install a single USS auth factor. If you want to set up multiple factors
  // for your test, call this multiple times.
  void InstallUssFactor(AuthFactor factor) {
    EXPECT_THAT(manager_.SaveAuthFactor(kObfuscatedUsername, factor), IsOk());
  }

  FakePlatform platform_;

  // Username used for all tests.
  static constexpr char kUsername[] = "user@testing.com";
  // Computing the obfuscated name requires the system salt from FakePlatform
  // and so this must be defined after it and not before.
  const std::string kObfuscatedUsername;

  StrictMock<MockKeysetManagement> keyset_management_;
  AuthFactorVaultKeysetConverter converter_{&keyset_management_};
  AuthFactorManager manager_{&platform_};
};

// Test that if nothing is set up, no factors are loaded (with or without USS).
TEST_F(LoadAuthFactorMapTest, NoFactors) {
  InstallVaultKeysets({});

  {
    auto no_uss = DisableUssExperiment();
    auto af_map = LoadAuthFactorMap(
        /*is_uss_migration_enabled=*/false, kObfuscatedUsername, platform_,
        converter_, manager_);

    EXPECT_THAT(af_map, IsEmpty());
  }

  {
    auto uss = EnableUssExperiment();
    auto af_map =
        LoadAuthFactorMap(/*is_uss_migration_enabled=*/false,
                          kObfuscatedUsername, platform_, converter_, manager_);

    EXPECT_THAT(af_map, IsEmpty());
  }
}

TEST_F(LoadAuthFactorMapTest, LoadWithOnlyVaultKeysets) {
  auto no_uss = DisableUssExperiment();
  InstallVaultKeysets({{"primary", &CreatePasswordVaultKeyset},
                       {"secondary", &CreatePasswordVaultKeyset}});

  auto af_map = LoadAuthFactorMap(
      /*is_uss_migration_enabled=*/false, kObfuscatedUsername, platform_,
      converter_, manager_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kVaultKeyset),
                  AuthFactorMapItem(AuthFactorType::kPassword, "secondary",
                                    AuthFactorStorageType::kVaultKeyset)));
}

TEST_F(LoadAuthFactorMapTest, LoadWithOnlyUss) {
  auto uss = EnableUssExperiment();
  InstallVaultKeysets({});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = PasswordAuthFactorMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));
  InstallUssFactor(AuthFactor(AuthFactorType::kPin, "secondary",
                              {.metadata = PinAuthFactorMetadata()},
                              {.state = PinWeaverAuthBlockState()}));
  auto af_map = LoadAuthFactorMap(
      /*is_uss_migration_enabled=*/false, kObfuscatedUsername, platform_,
      converter_, manager_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kUserSecretStash),
                  AuthFactorMapItem(AuthFactorType::kPin, "secondary",
                                    AuthFactorStorageType::kUserSecretStash)));
}

// Test that, given a mix of regular VKs, backup VKs, and USS factors, the
// correct ones are loaded depending on whether USS is enabled or disabled.
TEST_F(LoadAuthFactorMapTest, LoadWithMixUsesUssAndVk) {
  InstallVaultKeysets({{"tertiary", &CreatePasswordVaultKeyset},
                       {"quaternary", &CreateBackupVaultKeyset}});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = PasswordAuthFactorMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));
  InstallUssFactor(AuthFactor(AuthFactorType::kPin, "secondary",
                              {.metadata = PinAuthFactorMetadata()},
                              {.state = PinWeaverAuthBlockState()}));

  // Without USS, only the regular and backup VKs should be loaded.
  {
    auto no_uss = DisableUssExperiment();
    auto af_map = LoadAuthFactorMap(
        /*is_uss_migration_enabled=*/false, kObfuscatedUsername, platform_,
        converter_, manager_);

    EXPECT_THAT(af_map,
                UnorderedElementsAre(
                    AuthFactorMapItem(AuthFactorType::kPassword, "tertiary",
                                      AuthFactorStorageType::kVaultKeyset),
                    AuthFactorMapItem(AuthFactorType::kPassword, "quaternary",
                                      AuthFactorStorageType::kVaultKeyset)));
  }

  // With USS, the USS factors should be loaded along with the non-backup VKs.
  {
    auto uss = EnableUssExperiment();
    auto af_map = LoadAuthFactorMap(
        /*is_uss_migration_enabled=*/false, kObfuscatedUsername, platform_,
        converter_, manager_);

    EXPECT_THAT(af_map,
                UnorderedElementsAre(
                    AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                      AuthFactorStorageType::kUserSecretStash),
                    AuthFactorMapItem(AuthFactorType::kPin, "secondary",
                                      AuthFactorStorageType::kUserSecretStash),
                    AuthFactorMapItem(AuthFactorType::kPassword, "tertiary",
                                      AuthFactorStorageType::kVaultKeyset)));
  }
}

// Test that, given a mix of regular VKs, migrated VKs, and USS factors, the
// correct ones are loaded depending on whether USS migration is enabled or
// disabled.
TEST_F(LoadAuthFactorMapTest, LoadWithMixUsesUssAndMigratedVk) {
  InstallVaultKeysets({{"secondary", &CreatePasswordVaultKeyset},
                       {"primary", &CreateMigratedVaultKeyset}});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = PasswordAuthFactorMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));
  auto no_uss = EnableUssExperiment();

  // Without USS migration, only the regular and migrated VKs should be loaded.
  {
    auto af_map = LoadAuthFactorMap(
        /*is_uss_migration_enabled=*/false, kObfuscatedUsername, platform_,
        converter_, manager_);

    EXPECT_THAT(af_map,
                UnorderedElementsAre(
                    AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                      AuthFactorStorageType::kVaultKeyset),
                    AuthFactorMapItem(AuthFactorType::kPassword, "secondary",
                                      AuthFactorStorageType::kVaultKeyset)));
  }

  // With USS migration, the USS factors should be loaded along with the regular
  // VKs.
  {
    auto af_map = LoadAuthFactorMap(
        /*is_uss_migration_enabled=*/true, kObfuscatedUsername, platform_,
        converter_, manager_);

    EXPECT_THAT(af_map,
                UnorderedElementsAre(
                    AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                      AuthFactorStorageType::kUserSecretStash),
                    AuthFactorMapItem(AuthFactorType::kPassword, "secondary",
                                      AuthFactorStorageType::kVaultKeyset)));
  }
}

}  // namespace
}  // namespace cryptohome

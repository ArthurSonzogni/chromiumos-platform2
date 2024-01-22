// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/recoverable_key_store.h"

#include <optional>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/secure_box.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/recoverable_key_store/mock_backend_cert_provider.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::IsOkAndHolds;
using ::hwsec_foundation::error::testing::NotOk;
using ::testing::Return;

constexpr size_t kSecurityDomainWrappingKeySize = 32;
constexpr size_t kSaltSize = 32;
constexpr size_t kHashSize = 32;
constexpr uint64_t kCertListVersion = 1000;

std::optional<SecurityDomainKeys> GetValidSecurityDomainKeys() {
  const brillo::SecureBlob kSeed("seed_abc");
  const brillo::SecureBlob kWrappingKey(kSecurityDomainWrappingKeySize, 0xAA);
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed);
  if (!key_pair.has_value()) {
    return std::nullopt;
  }
  return SecurityDomainKeys{.key_pair = *key_pair,
                            .wrapping_key = kWrappingKey};
}

std::optional<RecoverableKeyStoreBackendCert> GetValidBackendCert() {
  const brillo::SecureBlob kSeed("seed_123");
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed);
  if (!key_pair.has_value()) {
    return std::nullopt;
  }
  return RecoverableKeyStoreBackendCert{
      .version = kCertListVersion,
      .public_key = key_pair->public_key,
  };
}

std::optional<RecoverableKeyStoreState> CreateStateWithVersion(
    uint64_t version) {
  RecoverableKeyStoreState state;
  RecoverableKeyStore store;
  std::string store_proto_string;
  store.mutable_key_store_metadata()->set_cert_list_version(version);
  if (!store.SerializeToString(&store_proto_string)) {
    return std::nullopt;
  }
  state.key_store_proto = brillo::BlobFromString(store_proto_string);
  return state;
}

}  // namespace

TEST(RecoverableKeyStoreTest, CreateSuccess) {
  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kHashSize, 0xAA),
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm =
                  SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234,
              .salt = brillo::Blob(kSaltSize, 0xBB),
              .should_generate_key_store = true,
          }}};

  std::optional<RecoverableKeyStoreBackendCert> backend_cert =
      GetValidBackendCert();
  ASSERT_TRUE(backend_cert.has_value());
  MockRecoverableKeyStoreBackendCertProvider cert_provider;
  EXPECT_CALL(cert_provider, GetBackendCert).WillOnce(Return(*backend_cert));

  CryptohomeStatusOr<RecoverableKeyStoreState> state =
      CreateRecoverableKeyStoreState(
          KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN, auth_input, metadata,
          cert_provider);
  ASSERT_THAT(state, IsOk());
  EXPECT_TRUE(RecoverableKeyStore().ParseFromString(
      brillo::BlobToString(state->key_store_proto)));
}

TEST(RecoverableKeyStoreTest, CreateInvalidParams) {
  AuthInput auth_input = {.user_input = brillo::SecureBlob(kHashSize, 0xAA)};

  MockRecoverableKeyStoreBackendCertProvider cert_provider;
  CryptohomeStatusOr<RecoverableKeyStoreState> state =
      CreateRecoverableKeyStoreState(
          KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN, auth_input, {},
          cert_provider);
  EXPECT_THAT(state, NotOk());
}

TEST(RecoverableKeyStoreTest, CreateGetCertFailed) {
  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kHashSize, 0xAA),
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm =
                  SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234,
              .salt = brillo::Blob(kSaltSize, 0xBB),
              .should_generate_key_store = true,
          }}};

  MockRecoverableKeyStoreBackendCertProvider cert_provider;
  EXPECT_CALL(cert_provider, GetBackendCert).WillOnce(Return(std::nullopt));

  CryptohomeStatusOr<RecoverableKeyStoreState> state =
      CreateRecoverableKeyStoreState(
          KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN, auth_input, metadata,
          cert_provider);
  EXPECT_THAT(state, NotOk());
}

TEST(RecoverableKeyStoreTest, UpdateSuccess) {
  std::optional<RecoverableKeyStoreState> state =
      CreateStateWithVersion(kCertListVersion - 1);
  ASSERT_TRUE(state.has_value());

  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kHashSize, 0xAA),
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm =
                  SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234,
              .salt = brillo::Blob(kSaltSize, 0xBB),
              .should_generate_key_store = true,
          }}};

  std::optional<RecoverableKeyStoreBackendCert> backend_cert =
      GetValidBackendCert();
  ASSERT_TRUE(backend_cert.has_value());
  MockRecoverableKeyStoreBackendCertProvider cert_provider;
  EXPECT_CALL(cert_provider, GetBackendCert).WillOnce(Return(*backend_cert));

  auto updated = MaybeUpdateRecoverableKeyStoreState(
      *state, KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN, auth_input,
      metadata, cert_provider);
  ASSERT_THAT(updated, IsOk());
  ASSERT_TRUE(updated->has_value());
  EXPECT_NE(state->key_store_proto, (*updated)->key_store_proto);
}

TEST(RecoverableKeyStoreTest, UpdateNotNeeded) {
  std::optional<RecoverableKeyStoreState> state =
      CreateStateWithVersion(kCertListVersion);
  ASSERT_TRUE(state.has_value());

  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kHashSize, 0xAA),
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm =
                  SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234,
              .salt = brillo::Blob(kSaltSize, 0xBB),
              .should_generate_key_store = true,
          }}};

  std::optional<RecoverableKeyStoreBackendCert> backend_cert =
      GetValidBackendCert();
  ASSERT_TRUE(backend_cert.has_value());
  MockRecoverableKeyStoreBackendCertProvider cert_provider;
  EXPECT_CALL(cert_provider, GetBackendCert).WillOnce(Return(*backend_cert));

  auto updated = MaybeUpdateRecoverableKeyStoreState(
      *state, KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN, auth_input,
      metadata, cert_provider);
  ASSERT_THAT(updated, IsOkAndHolds(std::nullopt));
}

TEST(RecoverableKeyStoreTest, UpdateFailed) {
  std::optional<RecoverableKeyStoreState> state =
      CreateStateWithVersion(kCertListVersion - 1);
  ASSERT_TRUE(state.has_value());
  brillo::Blob original_proto = state->key_store_proto;

  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kHashSize, 0xAA),
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm =
                  SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234,
              .salt = brillo::Blob(kSaltSize, 0xBB),
              .should_generate_key_store = true,
          }}};

  MockRecoverableKeyStoreBackendCertProvider cert_provider;
  EXPECT_CALL(cert_provider, GetBackendCert).WillOnce(Return(std::nullopt));

  auto updated = MaybeUpdateRecoverableKeyStoreState(
      *state, KnowledgeFactorType::KNOWLEDGE_FACTOR_TYPE_PIN, auth_input,
      metadata, cert_provider);
  EXPECT_THAT(updated, NotOk());
}

}  // namespace cryptohome

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/generate.h"

#include <optional>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/secure_box.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;

constexpr size_t kSecurityDomainWrappingKeySize = 32;
constexpr size_t kLabelSize = 8;
constexpr size_t kSaltSize = 32;
constexpr size_t kHashSize = 32;

}  // namespace

TEST(GenerateRecoverableKeyStoreTest, GenerateSuccess) {
  const brillo::SecureBlob kSeed1("seed1"), kSeed2("seed2");
  const brillo::SecureBlob kWrappingKey(kSecurityDomainWrappingKeySize, 0xAA);
  const LockScreenKnowledgeFactor kLskf = {
      .lskf_type =
          LockScreenKnowledgeFactorType::LOCK_SCREEN_KNOWLEDGE_FACTOR_TYPE_PIN,
      .algorithm =
          LockScreenKnowledgeFactorHashAlgorithm::HASH_TYPE_PBKDF2_AES256_1234,
      .salt = brillo::Blob(kSaltSize, 0xBB),
      .hash = brillo::SecureBlob(kHashSize, 0xCC),
  };
  const brillo::Blob kLabel(kLabelSize, 0xDD);

  // Construct a valid security domain key set.
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair1 =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed1);
  ASSERT_TRUE(key_pair1.has_value());
  SecurityDomainKeys security_domain_keys = {.key_pair = *key_pair1,
                                             .wrapping_key = kWrappingKey};

  // Construct a valid backend cert public key.
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair2 =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed2);
  ASSERT_TRUE(key_pair2.has_value());
  RecoverableKeyStoreBackendCert cert = {
      .version = 1000,
      .public_key = key_pair2->public_key,
  };

  EXPECT_THAT(
      GenerateRecoverableKeyStore(kLskf, kLabel, security_domain_keys, cert),
      IsOk());
}

TEST(GenerateRecoverableKeyStoreTest, GenerateInvalidPublicKey) {
  const brillo::SecureBlob kSeed("seed");
  const brillo::SecureBlob kWrappingKey(kSecurityDomainWrappingKeySize, 0xAA);
  const LockScreenKnowledgeFactor kLskf = {
      .lskf_type =
          LockScreenKnowledgeFactorType::LOCK_SCREEN_KNOWLEDGE_FACTOR_TYPE_PIN,
      .algorithm =
          LockScreenKnowledgeFactorHashAlgorithm::HASH_TYPE_PBKDF2_AES256_1234,
      .salt = brillo::Blob(kSaltSize, 0xBB),
      .hash = brillo::SecureBlob(kHashSize, 0xCC),
  };
  const brillo::Blob kLabel(kLabelSize, 0xDD);
  RecoverableKeyStoreBackendCert kInvalidCert = {
      .version = 1000,
      .public_key = brillo::Blob(65, 0xEE),
  };

  // Construct a valid security domain key set.
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed);
  ASSERT_TRUE(key_pair.has_value());
  SecurityDomainKeys security_domain_keys = {.key_pair = *key_pair,
                                             .wrapping_key = kWrappingKey};

  EXPECT_THAT(GenerateRecoverableKeyStore(kLskf, kLabel, security_domain_keys,
                                          kInvalidCert),
              NotOk());
}

}  // namespace cryptohome

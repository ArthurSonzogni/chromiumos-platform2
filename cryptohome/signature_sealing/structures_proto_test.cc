// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <absl/types/variant.h>
#include <gtest/gtest.h>

#include "cryptohome/signature_sealing/structures_proto.h"

using brillo::BlobFromString;

namespace cryptohome {

TEST(ChallengeSignatureAlgorithmTest, ToProtoFromProto) {
  for (auto algo : {
           structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1,
           structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
           structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384,
           structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512,
       }) {
    EXPECT_EQ(algo, proto::FromProto(proto::ToProto(algo)));
  }
}

TEST(ChallengeSignatureAlgorithmTest, FromProtoToProto) {
  for (auto algo : {
           ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA1,
           ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
           ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA384,
           ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA512,
       }) {
    EXPECT_EQ(algo, proto::ToProto(proto::FromProto(algo)));
  }
}

TEST(SignatureSealedDataTest, ToProtoFromProtoTPM2) {
  structure::Tpm2PolicySignedData data{
      .public_key_spki_der = BlobFromString("public_key_spki_der"),
      .srk_wrapped_secret = BlobFromString("srk_wrapped_secret"),
      .scheme = 0x54321,
      .hash_alg = 0x12345,
      .pcr_restrictions = {structure::Tpm2PcrRestriction{
          .pcr_values = {structure::PcrValue{
              .pcr_index = 4,
              .pcr_value = BlobFromString("pcr_value"),
          }},
          .policy_digest = BlobFromString("policy_digest"),
      }},
  };

  structure::SignatureSealedData struct_data = data;
  ASSERT_TRUE(
      absl::holds_alternative<structure::Tpm2PolicySignedData>(struct_data));

  structure::SignatureSealedData final_data =
      proto::FromProto(proto::ToProto(struct_data));
  ASSERT_TRUE(
      absl::holds_alternative<structure::Tpm2PolicySignedData>(final_data));

  const structure::Tpm2PolicySignedData& tpm2_data =
      absl::get<structure::Tpm2PolicySignedData>(final_data);

  EXPECT_EQ(tpm2_data.public_key_spki_der, data.public_key_spki_der);
  EXPECT_EQ(tpm2_data.srk_wrapped_secret, data.srk_wrapped_secret);
  EXPECT_EQ(tpm2_data.scheme, data.scheme);
  EXPECT_EQ(tpm2_data.hash_alg, data.hash_alg);
  ASSERT_EQ(tpm2_data.pcr_restrictions.size(), data.pcr_restrictions.size());
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].pcr_values.size(),
            data.pcr_restrictions[0].pcr_values.size());
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].pcr_values[0].pcr_index,
            data.pcr_restrictions[0].pcr_values[0].pcr_index);
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].pcr_values[0].pcr_value,
            data.pcr_restrictions[0].pcr_values[0].pcr_value);
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].policy_digest,
            data.pcr_restrictions[0].policy_digest);
}

TEST(SignatureSealedDataTest, ToProtoFromProtoTPM1) {
  structure::Tpm12CertifiedMigratableKeyData data{
      .public_key_spki_der = BlobFromString("public_key_spki_der"),
      .srk_wrapped_cmk = BlobFromString("srk_wrapped_cmk"),
      .cmk_pubkey = BlobFromString("cmk_pubkey"),
      .cmk_wrapped_auth_data = BlobFromString("cmk_wrapped_auth_data"),
      .pcr_bound_items = {structure::Tpm12PcrBoundItem{
          .pcr_values = {structure::PcrValue{
              .pcr_index = 4,
              .pcr_value = BlobFromString("pcr_value"),
          }},
          .bound_secret = BlobFromString("policy_digest"),
      }},
  };

  structure::SignatureSealedData struct_data = data;
  ASSERT_TRUE(
      absl::holds_alternative<structure::Tpm12CertifiedMigratableKeyData>(
          struct_data));

  structure::SignatureSealedData final_data =
      proto::FromProto(proto::ToProto(struct_data));
  ASSERT_TRUE(
      absl::holds_alternative<structure::Tpm12CertifiedMigratableKeyData>(
          final_data));

  const structure::Tpm12CertifiedMigratableKeyData& tpm1_data =
      absl::get<structure::Tpm12CertifiedMigratableKeyData>(final_data);

  EXPECT_EQ(tpm1_data.public_key_spki_der, data.public_key_spki_der);
  EXPECT_EQ(tpm1_data.srk_wrapped_cmk, data.srk_wrapped_cmk);
  EXPECT_EQ(tpm1_data.cmk_pubkey, data.cmk_pubkey);
  EXPECT_EQ(tpm1_data.cmk_wrapped_auth_data, data.cmk_wrapped_auth_data);
  ASSERT_EQ(tpm1_data.pcr_bound_items.size(), data.pcr_bound_items.size());
  EXPECT_EQ(tpm1_data.pcr_bound_items[0].pcr_values.size(),
            data.pcr_bound_items[0].pcr_values.size());
  EXPECT_EQ(tpm1_data.pcr_bound_items[0].pcr_values[0].pcr_index,
            data.pcr_bound_items[0].pcr_values[0].pcr_index);
  EXPECT_EQ(tpm1_data.pcr_bound_items[0].pcr_values[0].pcr_value,
            data.pcr_bound_items[0].pcr_values[0].pcr_value);
  EXPECT_EQ(tpm1_data.pcr_bound_items[0].bound_secret,
            data.pcr_bound_items[0].bound_secret);
}

TEST(SignatureChallengeInfoTest, ToProtoFromProto) {
  structure::Tpm2PolicySignedData policy_data = {
      .public_key_spki_der = BlobFromString("public_key_spki_der"),
      .srk_wrapped_secret = BlobFromString("srk_wrapped_secret"),
      .scheme = 0x54321,
      .hash_alg = 0x12345,
      .pcr_restrictions = {structure::Tpm2PcrRestriction{
          .pcr_values = {structure::PcrValue{
              .pcr_index = 4,
              .pcr_value = BlobFromString("pcr_value"),
          }},
          .policy_digest = BlobFromString("policy_digest"),
      }},
  };
  structure::SignatureChallengeInfo data{
      .public_key_spki_der = BlobFromString("public_key_spki_der"),
      .sealed_secret = policy_data,
      .salt = BlobFromString("salt"),
      .salt_signature_algorithm =
          structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384,
  };

  structure::SignatureChallengeInfo final_data =
      proto::FromProto(proto::ToProto(data));
  EXPECT_EQ(final_data.public_key_spki_der, data.public_key_spki_der);
  EXPECT_EQ(final_data.salt, data.salt);
  EXPECT_EQ(final_data.salt_signature_algorithm, data.salt_signature_algorithm);

  ASSERT_TRUE(absl::holds_alternative<structure::Tpm2PolicySignedData>(
      final_data.sealed_secret));
  const structure::Tpm2PolicySignedData& tpm2_data =
      absl::get<structure::Tpm2PolicySignedData>(final_data.sealed_secret);

  EXPECT_EQ(tpm2_data.public_key_spki_der, policy_data.public_key_spki_der);
  EXPECT_EQ(tpm2_data.srk_wrapped_secret, policy_data.srk_wrapped_secret);
  EXPECT_EQ(tpm2_data.scheme, policy_data.scheme);
  EXPECT_EQ(tpm2_data.hash_alg, policy_data.hash_alg);
  ASSERT_EQ(tpm2_data.pcr_restrictions.size(),
            policy_data.pcr_restrictions.size());
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].pcr_values.size(),
            policy_data.pcr_restrictions[0].pcr_values.size());
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].pcr_values[0].pcr_index,
            policy_data.pcr_restrictions[0].pcr_values[0].pcr_index);
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].pcr_values[0].pcr_value,
            policy_data.pcr_restrictions[0].pcr_values[0].pcr_value);
  EXPECT_EQ(tpm2_data.pcr_restrictions[0].policy_digest,
            policy_data.pcr_restrictions[0].policy_digest);
}

TEST(ChallengePublicKeyInfoTest, ToProtoFromProto) {
  structure::ChallengePublicKeyInfo data{
      .public_key_spki_der = BlobFromString("public_key_spki_der"),
      .signature_algorithm = {
          structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1,
          structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
      }};

  structure::ChallengePublicKeyInfo final_data =
      proto::FromProto(proto::ToProto(data));
  EXPECT_EQ(final_data.public_key_spki_der, data.public_key_spki_der);
  EXPECT_EQ(final_data.signature_algorithm, data.signature_algorithm);
}

}  // namespace cryptohome

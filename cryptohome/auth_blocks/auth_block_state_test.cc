// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

#include <optional>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/flatbuffer_schemas/auth_block_state_test_utils.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::SecureBlob;

namespace cryptohome {
namespace {
constexpr int kWorkFactor = 16384;
constexpr int kBlockSize = 8;
constexpr int kParallelFactor = 1;
const Blob kSalt = BlobFromString("salt");
const Blob kChapsSalt = BlobFromString("chaps_salt");
const Blob kResetSeedSalt = BlobFromString("reset_seed_salt");
}  // namespace

TEST(AuthBlockStateBindingTest, EmptyState) {
  AuthBlockState state;
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ScryptAuthBlockState) {
  AuthBlockState state = {.state = ScryptAuthBlockState{
                              .salt = kSalt,
                              .chaps_salt = kChapsSalt,
                              .reset_seed_salt = kResetSeedSalt,
                              .work_factor = kWorkFactor,
                              .block_size = kBlockSize,
                              .parallel_factor = kParallelFactor,
                          }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ScryptAuthBlockStateEmpty) {
  AuthBlockState state = {.state = ScryptAuthBlockState{}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, LibScryptCompatAuthBlockStateNotEqual) {
  AuthBlockState state = {.state = ScryptAuthBlockState{}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  state.state = ScryptAuthBlockState{
      .salt = BlobFromString(""),
      .chaps_salt = BlobFromString(""),
      .reset_seed_salt = BlobFromString(""),
      .work_factor = kWorkFactor,
      .block_size = kBlockSize,
      .parallel_factor = kParallelFactor,
  };
  EXPECT_NE(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmNotBoundToPcrAuthBlockState) {
  AuthBlockState state = {
      .state = TpmNotBoundToPcrAuthBlockState{
          .scrypt_derived = true,
          .salt = kSalt,
          .password_rounds = 1234,
          .tpm_key = BlobFromString("tpm_key"),
          .tpm_public_key_hash = BlobFromString("tpm_public_key_hash"),
      }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmNotBoundToPcrAuthBlockStateOptional) {
  AuthBlockState state1 = {.state = TpmNotBoundToPcrAuthBlockState{}};
  std::optional<Blob> blob1 = state1.Serialize();
  ASSERT_TRUE(blob1.has_value());
  std::optional<AuthBlockState> state1_new =
      AuthBlockState::Deserialize(blob1.value());
  ASSERT_TRUE(state1_new.has_value());
  EXPECT_EQ(state1, state1_new);

  AuthBlockState state2 = {.state = TpmNotBoundToPcrAuthBlockState{
                               .password_rounds = 0,
                           }};
  std::optional<Blob> blob2 = state2.Serialize();
  ASSERT_TRUE(blob2.has_value());
  std::optional<AuthBlockState> state2_new =
      AuthBlockState::Deserialize(blob2.value());
  ASSERT_TRUE(state2_new.has_value());
  EXPECT_EQ(state2, state2_new);

  AuthBlockState state3 = {.state = TpmNotBoundToPcrAuthBlockState{
                               .scrypt_derived = false,
                           }};
  std::optional<Blob> blob3 = state3.Serialize();
  ASSERT_TRUE(blob3.has_value());
  std::optional<AuthBlockState> state3_new =
      AuthBlockState::Deserialize(blob3.value());
  ASSERT_TRUE(state3_new.has_value());
  EXPECT_EQ(state3, state3_new);

  AuthBlockState state4 = {.state = TpmNotBoundToPcrAuthBlockState{
                               .scrypt_derived = false,
                               .password_rounds = 0,
                           }};
  std::optional<Blob> blob4 = state4.Serialize();
  ASSERT_TRUE(blob4.has_value());
  std::optional<AuthBlockState> state4_new =
      AuthBlockState::Deserialize(blob4.value());
  ASSERT_TRUE(state4_new.has_value());
  EXPECT_EQ(state4, state4_new);

  EXPECT_NE(state1, state2);
  EXPECT_NE(state1, state2_new);
  EXPECT_NE(state1_new, state2);
  EXPECT_NE(state1_new, state2_new);

  EXPECT_NE(state3, state4);
  EXPECT_NE(state3, state4_new);
  EXPECT_NE(state3_new, state4);
  EXPECT_NE(state3_new, state4_new);

  EXPECT_NE(state1, state3);
  EXPECT_NE(state2, state4);
  EXPECT_NE(state1, state3_new);
  EXPECT_NE(state2, state4_new);
  EXPECT_NE(state1_new, state3);
  EXPECT_NE(state2_new, state4);
  EXPECT_NE(state1_new, state3_new);
  EXPECT_NE(state2_new, state4_new);
}

TEST(AuthBlockStateBindingTest, TpmNotBoundToPcrAuthBlockStateEmpty) {
  AuthBlockState state = {.state = TpmNotBoundToPcrAuthBlockState{
                              .salt = BlobFromString(""),
                              .tpm_key = BlobFromString(""),
                              .tpm_public_key_hash = BlobFromString(""),
                          }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, DoubleWrappedCompatAuthBlockState) {
  AuthBlockState state = {
      .state = DoubleWrappedCompatAuthBlockState{
          .scrypt_state =
              ScryptAuthBlockState{
                  .salt = kSalt,
                  .chaps_salt = kChapsSalt,
                  .reset_seed_salt = kResetSeedSalt,
                  .work_factor = kWorkFactor,
                  .block_size = kBlockSize,
                  .parallel_factor = kParallelFactor,
              },
          .tpm_state = TpmNotBoundToPcrAuthBlockState{
              .scrypt_derived = true,
              .salt = kSalt,
              .password_rounds = 1234,
              .tpm_key = BlobFromString("tpm_key"),
              .tpm_public_key_hash = BlobFromString("tpm_public_key_hash"),
          }}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateTpm12) {
  AuthBlockState state = {
      .state =
          ChallengeCredentialAuthBlockState{
              .scrypt_state =
                  ScryptAuthBlockState{
                      .salt = kSalt,
                      .chaps_salt = kChapsSalt,
                      .reset_seed_salt = kResetSeedSalt,
                      .work_factor = kWorkFactor,
                      .block_size = kBlockSize,
                      .parallel_factor = kParallelFactor,
                  },
              .keyset_challenge_info = SerializedSignatureChallengeInfo{
                  .public_key_spki_der = BlobFromString("public_key_spki_der"),
                  .sealed_secret =
                      hwsec::Tpm12CertifiedMigratableKeyData{
                          .public_key_spki_der =
                              BlobFromString("public_key_spki_der"),
                          .srk_wrapped_cmk = BlobFromString("srk_wrapped_cmk"),
                          .cmk_pubkey = BlobFromString("cmk_pubkey"),
                          .cmk_wrapped_auth_data =
                              BlobFromString("cmk_wrapped_auth_data"),
                          .pcr_bound_items =
                              {
                                  hwsec::Tpm12PcrBoundItem{
                                      .pcr_values =
                                          {
                                              hwsec::Tpm12PcrValue{
                                                  .pcr_index = 4,
                                                  .pcr_value = BlobFromString(
                                                      "pcr_value1"),
                                              },
                                          },
                                      .bound_secret =
                                          BlobFromString("bound_secret0"),
                                  },
                                  hwsec::Tpm12PcrBoundItem{
                                      .pcr_values =
                                          {
                                              hwsec::Tpm12PcrValue{
                                                  .pcr_index = 4,
                                                  .pcr_value =
                                                      BlobFromString(
                                                          "pcr_value1"),
                                              },
                                          },
                                      .bound_secret = BlobFromString(
                                          "bound_secret1"),
                                  },
                              },
                      },
                  .salt = BlobFromString("salt"),
                  .salt_signature_algorithm =
                      SerializedChallengeSignatureAlgorithm::
                          kRsassaPkcs1V15Sha256,
              }}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateTpm2) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .scrypt_state =
              ScryptAuthBlockState{
                  .salt = kSalt,
                  .chaps_salt = kChapsSalt,
                  .reset_seed_salt = kResetSeedSalt,
                  .work_factor = kWorkFactor,
                  .block_size = kBlockSize,
                  .parallel_factor = kParallelFactor,
              },
          .keyset_challenge_info = SerializedSignatureChallengeInfo{
              .public_key_spki_der = BlobFromString("public_key_spki_der"),
              .sealed_secret =
                  hwsec::Tpm2PolicySignedData{
                      .public_key_spki_der =
                          BlobFromString("public_key_spki_der"),
                      .srk_wrapped_secret =
                          BlobFromString("srk_wrapped_secret"),
                      .scheme = 5566,
                      .hash_alg = 7788,
                      .pcr_policy_digests =
                          {
                              hwsec::Tpm2PolicyDigest{
                                  .digest = BlobFromString("digest0")},
                              hwsec::Tpm2PolicyDigest{
                                  .digest = BlobFromString("digest1")},
                          },
                  },
              .salt = BlobFromString("salt"),
              .salt_signature_algorithm =
                  SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
          }}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateEmpty) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .scrypt_state =
              ScryptAuthBlockState{
                  .salt = BlobFromString(""),
                  .chaps_salt = BlobFromString(""),
                  .reset_seed_salt = BlobFromString(""),
                  .work_factor = kWorkFactor,
                  .block_size = kBlockSize,
                  .parallel_factor = kParallelFactor,
              },
          .keyset_challenge_info = SerializedSignatureChallengeInfo{
              .public_key_spki_der = BlobFromString(""),
              .sealed_secret =
                  hwsec::Tpm2PolicySignedData{
                      .public_key_spki_der = BlobFromString(""),
                      .srk_wrapped_secret = BlobFromString(""),
                      .pcr_policy_digests =
                          {
                              hwsec::Tpm2PolicyDigest{.digest =
                                                          BlobFromString("")},
                              hwsec::Tpm2PolicyDigest{.digest =
                                                          BlobFromString("")},
                          },
                  },
              .salt = BlobFromString(""),
          }}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateNoInfo) {
  AuthBlockState state = {.state = ChallengeCredentialAuthBlockState{
                              .scrypt_state =
                                  ScryptAuthBlockState{
                                      .salt = kSalt,
                                      .chaps_salt = kChapsSalt,
                                      .reset_seed_salt = kResetSeedSalt,
                                      .work_factor = kWorkFactor,
                                      .block_size = kBlockSize,
                                      .parallel_factor = kParallelFactor,
                                  },
                          }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateDefault) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .keyset_challenge_info = SerializedSignatureChallengeInfo{
              .sealed_secret = hwsec::Tpm2PolicySignedData{},
          }}};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
  state.state = ChallengeCredentialAuthBlockState{
      .keyset_challenge_info = SerializedSignatureChallengeInfo{
          .public_key_spki_der = BlobFromString(""),
          .sealed_secret =
              hwsec::Tpm2PolicySignedData{
                  .public_key_spki_der = BlobFromString(""),
                  .srk_wrapped_secret = BlobFromString(""),
                  .pcr_policy_digests = {},
              },
          .salt = BlobFromString(""),
      }};
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmBoundToPcrAuthBlockState) {
  AuthBlockState state = {
      .state = TpmBoundToPcrAuthBlockState{
          .scrypt_derived = false,
          .salt = kSalt,
          .tpm_key = BlobFromString("tpm_key"),
          .extended_tpm_key = BlobFromString("extended_tpm_key"),
          .tpm_public_key_hash = BlobFromString("tpm_public_key_hash"),
      }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, PinWeaverAuthBlockState) {
  AuthBlockState state = {.state = PinWeaverAuthBlockState{
                              .le_label = 0x1337,
                              .salt = kSalt,
                              .chaps_iv = BlobFromString("chaps_iv"),
                              .fek_iv = BlobFromString("fek_iv"),
                          }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, CryptohomeRecoveryAuthBlockState) {
  AuthBlockState state = {.state = CryptohomeRecoveryAuthBlockState{
                              .hsm_payload = BlobFromString("hsm_payload"),
                              .encrypted_destination_share =
                                  BlobFromString("encrypted_destination_share"),
                              .channel_pub_key = Blob(),
                              .encrypted_channel_priv_key = Blob(),
                          }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmEccAuthBlockState) {
  AuthBlockState state = {
      .state = TpmEccAuthBlockState{
          .salt = kSalt,
          .vkk_iv = BlobFromString("vkk_iv"),
          .auth_value_rounds = 5,
          .sealed_hvkkm = BlobFromString("sealed_hvkkm"),
          .extended_sealed_hvkkm = BlobFromString("extended_sealed_hvkkm"),
          .tpm_public_key_hash = std::nullopt,
          .wrapped_reset_seed = BlobFromString("wrapped_reset_seed"),
      }};
  std::optional<Blob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}
}  // namespace cryptohome

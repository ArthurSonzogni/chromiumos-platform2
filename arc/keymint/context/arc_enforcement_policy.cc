// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <cstddef>
#include <vector>

#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>
#include <crypto/random.h>
#include <keymaster/android_keymaster_utils.h>
#include <keymaster/cppcose/cppcose.h>
#include <keymaster/km_openssl/ckdf.h>
#include <keymaster/UniquePtr.h>
#include <openssl/mem.h>
#include <openssl/sha.h>

namespace arc::keymint::context {

namespace {

constexpr size_t kSessionKeySize = 32;

// Maximum allowable clock skew for auth tokens with timestamps in the future.
// In VM and multi-core environments, clock readings between the authenticator
// and KeyMint can drift slightly.
constexpr uint64_t kMaxClockSkewMs = 2000;

uint64_t GetWallClockTimeMs() {
  return base::saturated_cast<uint64_t>(
      base::Time::Now().InMillisecondsSinceUnixEpoch());
}

}  // namespace

ArcEnforcementPolicy::ArcEnforcementPolicy(uint32_t max_access_time_map_size,
                                           uint32_t max_access_count_map_size)
    : SoftKeymasterEnforcement(max_access_time_map_size,
                               max_access_count_map_size) {
  session_key_ = crypto::RandBytesAsVector(kSessionKeySize);
}

ArcEnforcementPolicy::~ArcEnforcementPolicy() = default;

void ArcEnforcementPolicy::set_session_key_for_tests(
    const std::vector<uint8_t>& session_key) {
  session_key_ = session_key;
}

void ArcEnforcementPolicy::set_shared_hmac_key_for_tests(
    const std::vector<uint8_t>& key) {
  shared_hmac_key_ = key;
}

bool ArcEnforcementPolicy::activation_date_valid(
    uint64_t activation_date) const {
  return activation_date <= GetWallClockTimeMs();
}

bool ArcEnforcementPolicy::expiration_date_passed(
    uint64_t expiration_date) const {
  return expiration_date < GetWallClockTimeMs();
}

bool ArcEnforcementPolicy::auth_token_timed_out(const hw_auth_token_t& token,
                                                uint32_t timeout) const {
  if (timeout == 0) {
    return false;
  }

  uint64_t token_time = ::keymaster::ntoh(token.timestamp);
  uint64_t current_time = get_current_time_ms();

  if (token_time > current_time) {
    // Tolerate minor clock drift where token timestamp is slightly ahead.
    return (token_time - current_time) > kMaxClockSkewMs;
  }

  uint64_t timeout_ms = static_cast<uint64_t>(timeout) * 1000;
  return (current_time - token_time) > timeout_ms;
}

bool ArcEnforcementPolicy::ValidateTokenSignature(
    const hw_auth_token_t& token) const {
  if (shared_hmac_key_.empty()) {
    return false;
  }

  constexpr size_t kHashableLength = offsetof(hw_auth_token_t, hmac);
  static_assert(kHashableLength == 37,
                "hw_auth_token_t structure layout changed");

  std::vector<uint8_t> data_to_mac(
      reinterpret_cast<const uint8_t*>(&token),
      reinterpret_cast<const uint8_t*>(&token) + kHashableLength);

  auto hmac_result = cppcose::generateHmacSha256(shared_hmac_key_, data_to_mac);
  if (!hmac_result) {
    LOG(ERROR) << "Failed to compute HMAC for auth token: "
               << hmac_result.message();
    return false;
  }

  const auto& computed_mac = *hmac_result;
  if (computed_mac.size() != sizeof(token.hmac)) {
    return false;
  }

  return CRYPTO_memcmp(computed_mac.data(), token.hmac, sizeof(token.hmac)) ==
         0;
}

::keymaster::KmErrorOr<std::array<uint8_t, 32>>
ArcEnforcementPolicy::ComputeHmac(
    const std::vector<uint8_t>& input_data) const {
  auto cppcose_output = cppcose::generateHmacSha256(session_key_, input_data);
  if (!cppcose_output) {
    LOG(ERROR) << "Error generating MAC: " << cppcose_output.message();
    ::keymaster::KmErrorOr<std::array<uint8_t, 32>> error(
        KM_ERROR_UNKNOWN_ERROR);
    return error;
  }
  auto cppcose_output_value = cppcose_output.moveValue();
  std::array<uint8_t, 32> output_array;

  if (output_array.size() != cppcose_output_value.size()) {
    LOG(ERROR) << "Error in copying cppcose output. Size is different ";
    ::keymaster::KmErrorOr<std::array<uint8_t, 32>> error(
        KM_ERROR_UNKNOWN_ERROR);
    return error;
  }

  std::copy_n(cppcose_output_value.begin(), cppcose_output_value.size(),
              output_array.begin());

  ::keymaster::KmErrorOr output(output_array);
  return output;
}

keymaster_error_t ArcEnforcementPolicy::ComputeSharedHmac(
    const ::keymaster::HmacSharingParametersArray& params_array,
    ::keymaster::KeymasterBlob* sharingCheck) {
  keymaster_error_t error =
      SoftKeymasterEnforcement::ComputeSharedHmac(params_array, sharingCheck);
  if (error != KM_ERROR_OK) {
    return error;
  }

  size_t num_chunks = params_array.num_params * 2;
  ::keymaster::UniquePtr<keymaster_blob_t[]> context_chunks(
      new (std::nothrow) keymaster_blob_t[num_chunks]);
  if (!context_chunks) {
    return KM_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  auto context_chunks_pos = context_chunks.get();
  for (const auto& params : ::keymaster::array_range(params_array.params_array,
                                                     params_array.num_params)) {
    *context_chunks_pos++ = params.seed;
    *context_chunks_pos++ = {params.nonce, sizeof(params.nonce)};
  }

  ::keymaster::KeymasterKeyBlob hmac_key;
  if (!hmac_key.Reset(SHA256_DIGEST_LENGTH)) {
    return KM_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  // Both SoftKeymasterEnforcement::ComputeSharedHmac (called above) and the
  // guest software Gatekeeper use AOSP's all-zero kFakeKeyAgreementKey. Using
  // the same key agreement key here ensures shared_hmac_key_ matches
  // sharing_check and guest-minted auth tokens. CKDF is re-run here because
  // SoftKeymasterEnforcement::hmac_key_ is private.
  constexpr uint8_t kFakeKeyAgreementKey[32] = {};
  constexpr const char kSharedHmacLabel[] = "KeymasterSharedMac";

  error =
      ::keymaster::ckdf(::keymaster::KeymasterKeyBlob(
                            kFakeKeyAgreementKey, sizeof(kFakeKeyAgreementKey)),
                        ::keymaster::KeymasterBlob(
                            reinterpret_cast<const uint8_t*>(kSharedHmacLabel),
                            strlen(kSharedHmacLabel)),
                        context_chunks.get(), num_chunks, &hmac_key);
  if (error != KM_ERROR_OK) {
    return error;
  }

  shared_hmac_key_.assign(hmac_key.key_material,
                          hmac_key.key_material + hmac_key.key_material_size);
  return KM_ERROR_OK;
}

}  // namespace arc::keymint::context

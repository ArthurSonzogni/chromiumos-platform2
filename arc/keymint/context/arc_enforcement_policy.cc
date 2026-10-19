// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>
#include <crypto/random.h>
#include <keymaster/cppcose/cppcose.h>

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
    // Per-operation auth token, not time-based.
    return false;
  }

  uint64_t now_ms = get_current_time_ms();
  uint64_t token_time = ::keymaster::ntoh(token.timestamp);

  if (token_time > now_ms) {
    // Tolerate minor clock drift where token timestamp is slightly ahead.
    return (token_time - now_ms) > kMaxClockSkewMs;
  }

  uint64_t timeout_ms = static_cast<uint64_t>(timeout) * 1000;
  return (now_ms - token_time) > timeout_ms;
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

}  // namespace arc::keymint::context

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_enforcement_policy.h"

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/random.h>
#include <keymaster/cppcose/cppcose.h>

namespace arc::keymint::context {

namespace {

constexpr size_t kSessionKeySize = 32;

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

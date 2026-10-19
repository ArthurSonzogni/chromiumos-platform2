// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_ENFORCEMENT_POLICY_H_
#define ARC_KEYMINT_CONTEXT_ARC_ENFORCEMENT_POLICY_H_

#include <vector>

#include <keymaster/km_openssl/soft_keymaster_enforcement.h>

namespace arc::keymint::context {

// Defines specific behavior for ARC KeyMint Enforcement Policy in ChromeOS.
class ArcEnforcementPolicy : public ::keymaster::SoftKeymasterEnforcement {
 public:
  // Disable default constructor.
  ArcEnforcementPolicy() = delete;
  explicit ArcEnforcementPolicy(uint32_t max_access_time_map_size,
                                uint32_t max_access_count_map_size);
  ~ArcEnforcementPolicy() override;
  // Not copyable nor assignable.
  ArcEnforcementPolicy(const ArcEnforcementPolicy&) = delete;
  ArcEnforcementPolicy& operator=(const ArcEnforcementPolicy&) = delete;

  bool activation_date_valid(uint64_t activation_date) const override;
  bool expiration_date_passed(uint64_t expiration_date) const override;
  bool auth_token_timed_out(const hw_auth_token_t& token,
                            uint32_t timeout) const override;

  ::keymaster::KmErrorOr<std::array<uint8_t, 32>> ComputeHmac(
      const std::vector<uint8_t>&) const override;

 private:
  void set_session_key_for_tests(const std::vector<uint8_t>& session_key);

  std::vector<uint8_t> session_key_;
  friend class ArcEnforcementPolicyTestPeer;
};
}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_ENFORCEMENT_POLICY_H_

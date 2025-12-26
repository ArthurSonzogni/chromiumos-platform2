// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_ATTESTATION_CONTEXT_H_
#define ARC_KEYMINT_CONTEXT_ARC_ATTESTATION_CONTEXT_H_

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <keymaster/contexts/soft_attestation_context.h>

namespace arc::keymint::context {

enum class VerifiedBootState {
  kUnverifiedBoot,
  kVerifiedBoot,
};

enum class VerifiedBootDeviceState {
  kUnlockedDevice,
  kLockedDevice,
};

// Defines specific behavior for ARC Attestation Context in ChromeOS.
class ArcAttestationContext : public ::keymaster::SoftAttestationContext {
 public:
  // Disable default constructor.
  ArcAttestationContext() = delete;
  explicit ArcAttestationContext(::keymaster::KmVersion km_version,
                                 keymaster_security_level_t security_level);
  ~ArcAttestationContext() override;
  // Not copyable nor assignable.
  ArcAttestationContext(const ArcAttestationContext&) = delete;
  ArcAttestationContext& operator=(const ArcAttestationContext&) = delete;

  keymaster_security_level_t GetSecurityLevel() const override;

  const VerifiedBootParams* GetVerifiedBootParams(
      keymaster_error_t* error) const override;

  keymaster_error_t SetVerifiedBootParams(
      std::string_view boot_state,
      std::string_view bootloader_state,
      const std::vector<uint8_t>& vbmeta_digest,
      std::optional<std::vector<uint8_t>> boot_key);

 private:
  keymaster_security_level_t security_level_;
  std::optional<std::string> bootloader_state_;
  std::optional<std::string> verified_boot_state_;
  std::optional<std::vector<uint8_t>> vbmeta_digest_;
  std::optional<std::vector<uint8_t>> boot_key_;
};
}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_ATTESTATION_CONTEXT_H_

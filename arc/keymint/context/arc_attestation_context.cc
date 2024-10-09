// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_attestation_context.h"

namespace arc::keymint::context {

ArcAttestationContext::ArcAttestationContext(
    ::keymaster::KmVersion km_version,
    keymaster_security_level_t security_level)
    : SoftAttestationContext(km_version), security_level_(security_level) {}

ArcAttestationContext::~ArcAttestationContext() = default;

keymaster_security_level_t ArcAttestationContext::GetSecurityLevel() const {
  return security_level_;
}
}  // namespace arc::keymint::context

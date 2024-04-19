// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_REMOTE_PROVISIONING_CONTEXT_H_
#define ARC_KEYMINT_CONTEXT_ARC_REMOTE_PROVISIONING_CONTEXT_H_

#include <cppbor.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>

#include <optional>
#include <utility>
#include <vector>

#include "arc/keymint/context/openssl_utils.h"

namespace arc::keymint::context {

// BCCPayload labels are based on |ProtectedData.aidl|.
enum BccPayloadLabel : int {
  ISSUER = 1,
  SUBJECT = 2,
  SUBJECT_PUBLIC_KEY = -4670552,
  KEY_USAGE = -4670553,
  CODE_HASH = -4670545,
  CODE_DESCRIPTOR = -4670546,
  CONFIG_HASH = -4670547,
  CONFIG_DESCRIPTOR = -4670548,
  CONFIG_COMPONENT_NAME = -70002,
  CONFIG_FIRMWARE_VERSION = -70003,
  CONFIG_RESETTABLE = -70004,
  AUTHORITY_HASH = -4670549,
  AUTHORITY_DESCRIPTOR = -4670550,
  MODE = -4670551,
};

// These functions are created on the lines of similar functions
// in libcppcose.
cppcose::ErrMsgOr<std::vector<uint8_t>> createCoseSign1SignatureFromDK(
    const std::vector<uint8_t>& protectedParams,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData);

cppcose::ErrMsgOr<cppbor::Array> constructCoseSign1FromDK(
    cppbor::Map protectedParams,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData);

// Defines specific behavior for ARC Remote Provisioning Context in ChromeOS.
class ArcRemoteProvisioningContext
    : public ::keymaster::PureSoftRemoteProvisioningContext {
 public:
  // Disable default constructor.
  ArcRemoteProvisioningContext() = delete;
  explicit ArcRemoteProvisioningContext(
      keymaster_security_level_t security_level);
  ~ArcRemoteProvisioningContext() override;
  // Not copyable nor assignable.
  ArcRemoteProvisioningContext(const ArcRemoteProvisioningContext&) = delete;
  ArcRemoteProvisioningContext& operator=(const ArcRemoteProvisioningContext&) =
      delete;

  std::optional<cppbor::Array> GenerateBcc(bool test_mode) const;
};
}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_REMOTE_PROVISIONING_CONTEXT_H_

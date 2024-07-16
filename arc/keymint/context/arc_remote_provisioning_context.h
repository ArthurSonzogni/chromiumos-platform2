// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_REMOTE_PROVISIONING_CONTEXT_H_
#define ARC_KEYMINT_CONTEXT_ARC_REMOTE_PROVISIONING_CONTEXT_H_

#include <cppbor.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
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

std::unique_ptr<cppbor::Map> CreateDeviceInfoMap(
    std::string& properties_content);

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

  // On failure, returns std::nullopt.
  // On success, returns a pair {private_key, CBOR Array}.
  // |private_key| has a value only in test mode. In production mode,
  // it is an empty vector.
  // CBOR Array carries the Cose Key, and a signed payload.
  std::optional<std::pair<std::vector<uint8_t>, cppbor::Array>> GenerateBcc(
      bool test_mode) const;

  std::unique_ptr<cppbor::Map> CreateDeviceInfo() const override;

  cppcose::ErrMsgOr<std::vector<uint8_t>> BuildProtectedDataPayload(
      bool test_mode,
      const std::vector<uint8_t>& mac_key,
      const std::vector<uint8_t>& additional_auth_data) const override;

  void SetSystemVersion(uint32_t os_version, uint32_t os_patchlevel);
  /* To avoid replay attacks, Android provides an input challenge for generating
   certificate request. This method sets the same challenge here, and it will
   be used in getting a ChromeOS quoted blob from libarc-attestation.
  */
  void SetChallengeForCertificateRequest(std::vector<uint8_t>& challenge);

 private:
  // Initialize the BCC if it has not yet happened.
  void ArcLazyInitProdBcc() const;
  keymaster_security_level_t security_level_;
  std::optional<uint32_t> os_version_;
  std::optional<uint32_t> os_patchlevel_;
  std::optional<std::vector<uint8_t>> certificate_challenge_;

  mutable std::once_flag bcc_initialized_flag_;
  mutable cppbor::Array boot_cert_chain_;

  base::FilePath property_dir_;
  void set_property_dir_for_tests(base::FilePath& path);
  friend class ArcRemoteProvisioningContextTestPeer;
};
}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_REMOTE_PROVISIONING_CONTEXT_H_

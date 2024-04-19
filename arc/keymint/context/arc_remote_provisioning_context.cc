// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

#include <keymaster/cppcose/cppcose.h>
#include <libarc-attestation/lib/interface.h>

#include <algorithm>
#include <string>

#include <base/logging.h>
#include <openssl/rand.h>

/*
A lot of data structures in this file mimic the structures in
|ProtectedData.aidl| -
https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/ProtectedData.aidl.
*/
namespace arc::keymint::context {

constexpr uint32_t kP256AffinePointSize = 32;
constexpr uint32_t kP256SignatureLength = 64;
const std::vector<uint8_t> kBccPayloadKeyUsage{0x20};

/*
This function creates BccEntryInput and then returns it after signing
by the key from CrOS DK cert.
*/
cppcose::ErrMsgOr<std::vector<uint8_t>> createCoseSign1SignatureFromDK(
    const std::vector<uint8_t>& protectedParams,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData) {
  // |signatureInput| is the BccEntryInput structure for |ProtectedData.aidl|.
  std::vector<uint8_t> signatureInput = cppbor::Array()
                                            .add("Signature1")
                                            .add(protectedParams)
                                            .add(additionalAuthData)
                                            .add(payload)
                                            .encode();

  std::vector<uint8_t> ecdsaDERSignature(kP256SignatureLength);
  arc_attestation::AndroidStatus status =
      arc_attestation::SignWithP256Dk(signatureInput, ecdsaDERSignature);

  if (!status.is_ok()) {
    LOG(ERROR) << "Signing by libarc-attestation failed";
    int32_t error_code = status.get_error_code();
    std::string error = "Error Message = " + status.get_message() +
                        ", Error Code = " + std::to_string(error_code);
    return error;
  }

  // The signature returned from libarc-attestation is in DER format.
  // Convert it to COSE Format.
  cppcose::ErrMsgOr<std::vector<uint8_t>> p256DkSignature =
      cppcose::ecdsaDerSignatureToCose(ecdsaDERSignature);

  if (!p256DkSignature) {
    auto errorMessage = p256DkSignature.moveMessage();
    LOG(ERROR) << "Error extracting Cose Signature from Chrome OS ECDSA Der "
                  "Signature: "
               << errorMessage;
    return errorMessage;
  }
  return p256DkSignature;
}

/*
This function returns BccEntry as in |ProtectedData.aidl|
*/
cppcose::ErrMsgOr<cppbor::Array> constructCoseSign1FromDK(
    cppbor::Map protectedParamsMap,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData) {
  std::vector<uint8_t> protectedParams =
      protectedParamsMap.add(cppcose::ALGORITHM, cppcose::ES256)
          .canonicalize()
          .encode();

  // |signature| represents BccEntryInput from |ProtectedtData.aidl|.
  auto signature = createCoseSign1SignatureFromDK(protectedParams, payload,
                                                  additionalAuthData);
  if (!signature) {
    return signature.moveMessage();
  }

  // Unprotected Parameters.
  auto unprotectedParams = cppbor::Map();

  // Returns the Bcc Entry.
  return cppbor::Array()
      .add(std::move(protectedParams))
      .add(std::move(unprotectedParams))
      .add(std::move(payload))
      .add(signature.moveValue());
}

namespace {}  // namespace

ArcRemoteProvisioningContext::ArcRemoteProvisioningContext(
    keymaster_security_level_t security_level)
    : PureSoftRemoteProvisioningContext(security_level) {}

ArcRemoteProvisioningContext::~ArcRemoteProvisioningContext() = default;

std::optional<cppbor::Array> ArcRemoteProvisioningContext::GenerateBcc(
    bool test_mode) const {
  // Provision certificate.
  arc_attestation::AndroidStatus provision_status =
      arc_attestation::ProvisionDkCert(true /*blocking*/);
  if (!provision_status.is_ok()) {
    LOG(ERROR) << "Error in Provisioning Dk Cert from libarc-attestation";
    return std::nullopt;
  }

  // Extract DK Cert Chain from libarc-attestation.
  std::vector<std::vector<uint8_t>> cert_chain;
  arc_attestation::AndroidStatus cert_status =
      arc_attestation::GetDkCertChain(cert_chain);
  if (!cert_status.is_ok()) {
    LOG(ERROR) << "Error in fetching DK Cert Chain from libarc-attestation";
    return std::nullopt;
  }

  if (cert_chain.size() == 0) {
    LOG(ERROR) << "DK Cert Chain from libarc-attestation is empty";
    return std::nullopt;
  }
  std::vector<uint8_t> uds_pub = cert_chain.front();

  // Extract Affine coordinates from libarc-attestation certificate.
  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);
  auto error = GetEcdsa256KeyFromCertBlob(uds_pub, x_coord, y_coord);
  if (error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to extract Affine coordinates from ChromeOS cert";
    return std::nullopt;
  }

  auto coseKey =
      cppbor::Map()
          .add(cppcose::CoseKey::KEY_TYPE, cppcose::EC2)
          .add(cppcose::CoseKey::ALGORITHM, cppcose::ES256)
          .add(cppcose::CoseKey::CURVE, cppcose::P256)
          .add(cppcose::CoseKey::KEY_OPS, cppbor::Array().add(cppcose::VERIFY))
          .add(cppcose::CoseKey::PUBKEY_X, x_vect)
          .add(cppcose::CoseKey::PUBKEY_Y, y_vect)
          .canonicalize();

  // This map is based on the Protected Data AIDL, which is further based on
  // the Open Profile for DICE.
  // |sign1Payload| represents BccPayload data structure from
  // |ProtectedData.aidl|. Fields - Issuer and Subject are redundant for a
  // degenerate Bcc chain like here.
  auto sign1Payload =
      cppbor::Map()
          .add(BccPayloadLabel::ISSUER, "Issuer")
          .add(BccPayloadLabel::SUBJECT, "Subject")
          .add(BccPayloadLabel::SUBJECT_PUBLIC_KEY, coseKey.encode())
          .add(BccPayloadLabel::KEY_USAGE, kBccPayloadKeyUsage)
          .canonicalize()
          .encode();
  std::vector<uint8_t> additional_authenticated_data;
  // |coseSign1| represents the Bcc entry.
  auto coseSign1 = constructCoseSign1FromDK(cppbor::Map(), sign1Payload,
                                            additional_authenticated_data);
  if (!coseSign1) {
    LOG(ERROR) << "Bcc Generation failed: " << coseSign1.moveMessage();
    return std::nullopt;
  }

  // Boot Certificate Chain.
  return cppbor::Array().add(std::move(coseKey)).add(coseSign1.moveValue());
}

}  // namespace arc::keymint::context

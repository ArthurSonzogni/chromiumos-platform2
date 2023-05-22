// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libarc-attestation/lib/provisioner.h>

#include <string>
#include <vector>

#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>

using brillo::BlobFromString;
using brillo::BlobToString;

namespace arc_attestation {

namespace {

// The timeout for GetCertificate() which can take a while.
constexpr base::TimeDelta kGetCertificateTimeout = base::Seconds(60);
// 15s for ECDSA signature should be plenty.
constexpr base::TimeDelta kSignTimeout = base::Seconds(15);
// Label in attestationd for the TPM Certifying Key.
constexpr char kTpmCertifyingKeyLabel[] = "tpm-certifying-key";
// Label in attestationd for the ARC Attestation Device Key.
constexpr char kArcAttestationDeviceKeyLabel[] = "arc-attestation-device-key";

constexpr char kEndOfCertForPem[] = "-----END CERTIFICATE-----";

void SplitPEMCerts(const std::string& bundle_in,
                   std::vector<std::string>& certs_out) {
  certs_out.clear();
  std::vector<std::string> accumulated;
  for (const std::string& s : base::SplitString(
           bundle_in, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    accumulated.push_back(s);
    if (base::TrimWhitespaceASCII(s, base::TRIM_ALL) == kEndOfCertForPem) {
      std::string current_cert = base::JoinString(accumulated, "\n") + "\n";
      accumulated.clear();
      certs_out.push_back(current_cert);
    }
  }
}

}  // namespace

bool Provisioner::IsOnRunner() {
  return runner_->RunsTasksInCurrentSequence();
}

AndroidStatus Provisioner::ProvisionCert() {
  // Already provisioned? Then no need to do it again.
  if (is_provisioned()) {
    return AndroidStatus::ok();
  }
  // Need dbus to provision.
  if (!EnsureDbus()) {
    LOG(ERROR) << "DBus is not available in Provisioner::ProvisionCert()";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  AndroidStatus result = ProvisionCertifyingKey();
  if (!result.is_ok()) {
    return result;
  }

  result = ProvisionArcAttestationDeviceKey();
  if (!result.is_ok()) {
    return result;
  }

  provisioned_.store(true);
  return AndroidStatus::ok();
}

AndroidStatus Provisioner::ProvisionCertifyingKey() {
  attestation::GetCertificateRequest request;
  request.set_certificate_profile(
      attestation::CertificateProfile::ARC_TPM_CERTIFYING_KEY_CERTIFICATE);
  request.set_aca_type(attestation::ACAType::DEFAULT_ACA);
  request.set_key_type(attestation::KeyType::KEY_TYPE_ECC);
  request.set_key_label(kTpmCertifyingKeyLabel);
  request.set_shall_trigger_enrollment(true);

  attestation::GetCertificateReply reply;
  brillo::ErrorPtr err;
  CHECK(proxy_);
  if (!proxy_->GetCertificate(request, &reply, &err,
                              kGetCertificateTimeout.InMilliseconds())) {
    // DBus call failed.
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  // Examine the result.
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    // The method call failed.
    LOG(ERROR)
        << "GetCertificate() call during ProvisionCertifyingKey() failed";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  tck_data_ = reply;
  return AndroidStatus::ok();
}

AndroidStatus Provisioner::ProvisionArcAttestationDeviceKey() {
  attestation::GetCertificateRequest request;
  request.set_certificate_profile(
      attestation::CertificateProfile::ARC_ATTESTATION_DEVICE_KEY_CERTIFICATE);
  request.set_aca_type(attestation::ACAType::DEFAULT_ACA);
  request.set_key_type(attestation::KeyType::KEY_TYPE_ECC);
  request.set_key_label(kArcAttestationDeviceKeyLabel);
  request.set_shall_trigger_enrollment(true);

  attestation::GetCertificateReply reply;
  brillo::ErrorPtr err;
  CHECK(proxy_);
  if (!proxy_->GetCertificate(request, &reply, &err,
                              kGetCertificateTimeout.InMilliseconds())) {
    // DBus call failed.
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  // Examine the result.
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    // The method call failed.
    LOG(ERROR) << "GetCertificate() call during "
                  "ProvisionArcAttestationDeviceKey() failed";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  aadk_data_ = reply;
  return AndroidStatus::ok();
}

bool Provisioner::EnsureDbus() {
  DCHECK(IsOnRunner());

  if (proxy_)
    return true;

  // Retry up to 3 times.
  for (int i = 0; i < 3; i++) {
    if (EnsureDbusInternal())
      return true;
  }
  return false;
}

bool Provisioner::EnsureDbusInternal() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = base::MakeRefCounted<dbus::Bus>(options);
  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to the system D-Bus in arc_attestation";
    bus_.reset();
    return false;
  }

  proxy_ = std::make_unique<org::chromium::AttestationProxy>(bus_);
  return true;
}

AndroidStatus Provisioner::GetDkCertChain(
    std::vector<std::vector<uint8_t>>& cert_out) {
  if (!is_provisioned()) {
    LOG(ERROR) << "Attempting to retrieve DK certificate without successful "
                  "provision.";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }
  // If we're provisioned, aadk_data_ should have value.
  CHECK(aadk_data_.has_value());

  std::vector<std::string> splitted_certs;
  SplitPEMCerts(aadk_data_->certificate(), splitted_certs);
  cert_out.clear();
  for (const std::string& s : splitted_certs) {
    cert_out.push_back(BlobFromString(s));
  }
  return AndroidStatus::ok();
}

AndroidStatus Provisioner::SignWithP256Dk(const std::vector<uint8_t>& input,
                                          std::vector<uint8_t>& signature) {
  if (!is_provisioned()) {
    LOG(ERROR) << "Attempting to sign with DK without successful provision.";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }
  if (!EnsureDbus()) {
    LOG(ERROR) << "DBus is not available in Provisioner::SignWithP256Dk()";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  attestation::SignRequest request;
  request.set_key_label(kArcAttestationDeviceKeyLabel);
  request.set_data_to_sign(BlobToString(input));

  attestation::SignReply reply;
  brillo::ErrorPtr err;
  if (!proxy_->Sign(request, &reply, &err, kSignTimeout.InMilliseconds())) {
    // DBus call failed.
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  // Examine the result.
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    // The method call failed.
    LOG(ERROR) << "Sign() call during SignWithP256Dk() failed";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  signature = BlobFromString(reply.signature());
  return AndroidStatus::ok();
}

std::optional<std::string> Provisioner::GetTpmCertifyingKeyBlob() {
  DCHECK(IsOnRunner());

  if (!is_provisioned()) {
    LOG(ERROR)
        << "Unable to fetch TPM Certifying Key Blob without provisioning keys";
    return std::nullopt;
  }
  // If we're provisioned, then tck_data_ should have value.
  CHECK(tck_data_);

  return tck_data_->key_blob();
}

std::optional<std::string> Provisioner::GetTpmCertifyingKeyCert() {
  DCHECK(IsOnRunner());

  if (!is_provisioned()) {
    LOG(ERROR)
        << "Unable to fetch TPM Certifying Key Cert without provisioning keys";
    return std::nullopt;
  }
  // If we're provisioned, then tck_data_ should have value.
  CHECK(tck_data_);

  return tck_data_->certificate();
}

}  // namespace arc_attestation

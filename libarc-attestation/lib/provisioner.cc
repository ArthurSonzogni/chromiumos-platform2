// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <libarc-attestation/lib/provisioner.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

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

  if (proxy_) {
    return true;
  }

  // Retry up to 3 times.
  for (int i = 0; i < 3; i++) {
    if (EnsureDbusInternal()) {
      return true;
    }
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

AndroidStatus Provisioner::GetEndorsementPublicKey(
    std::vector<uint8_t>& ek_public_key_out) {
  if (!EnsureDbus()) {
    LOG(ERROR)
        << "DBus is not available in Provisioner::GetEndorsementPublicKey()";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  attestation::GetEndorsementInfoRequest request;

  attestation::GetEndorsementInfoReply reply;
  brillo::ErrorPtr err;
  if (!proxy_->GetEndorsementInfo(request, &reply, &err,
                                  kSignTimeout.InMilliseconds())) {
    // DBus call failed.
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  // Examine the result.
  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    // The method call failed.
    LOG(ERROR) << "GetEndorsementInfo() call during "
                  "Provisioner::GetEndorsementPublicKey() failed";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  // Examine if reply carries the Endorsement Key.
  if (!reply.has_ek_public_key()) {
    LOG(ERROR)
        << "Reply from GetEndorsementInfo() does not carry Endorsement Key";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::INVALID_KEY_BLOB);
  }

  ek_public_key_out = brillo::BlobFromString(reply.ek_public_key());
  return AndroidStatus::ok();
}

bool Provisioner::GetCertificateFields(const std::string& pem_cert,
                                       std::string* subject_out,
                                       std::string* issue_date_out) {
  // Ensure output pointers are valid.
  if (!subject_out || !issue_date_out) {
    LOG(ERROR) << "decodePemCertificate: Output pointers cannot be null.";
    return false;
  }
  // Clear previous output values.
  subject_out->clear();
  issue_date_out->clear();

  // Create X509 object.
  crypto::ScopedBIO bio(
      BIO_new_mem_buf(const_cast<char*>(pem_cert.data()), pem_cert.size()));
  if (!bio) {
    LOG(WARNING) << __func__ << ": Failed to create mem BIO";
    return false;
  }

  crypto::ScopedX509 x509(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!x509) {
    LOG(WARNING) << __func__ << ": Failed to call PEM_read_bio_X509";
    return false;
  }

  // Extract Certificate Subject.
  unsigned char* subject_buffer = nullptr;
  int length =
      i2d_X509_NAME(X509_get_subject_name(x509.get()), &subject_buffer);
  crypto::ScopedOpenSSLBytes scoped_subject_buffer(subject_buffer);
  if (length <= 0) {
    LOG(WARNING) << "Pkcs11KeyStore: Failed to encode certificate subject.";
    return false;
  }

  X509_NAME* subject_name = X509_get_subject_name(x509.get());
  if (!subject_name) {
    LOG(WARNING) << "Pkcs11KeyStore: Certificate has no subject name.";
    subject_out->clear();
    return false;
  }

  char* oneline_subject = X509_NAME_oneline(subject_name, nullptr, 0);
  if (!oneline_subject) {
    LOG(WARNING) << "Pkcs11KeyStore: Failed to get subject oneline length.";
    return false;
  }

  crypto::ScopedOpenSSLBytes scoped_oneline_subject(
      reinterpret_cast<unsigned char*>(oneline_subject));
  subject_out->assign(reinterpret_cast<char*>(scoped_oneline_subject.get()));

  // Extract Certificate Issue Date.
  ASN1_TIME* not_before = X509_get_notBefore(x509.get());
  if (!not_before) {
    LOG(WARNING) << "Failed to get certificate issue date.";
    return false;
  }

  crypto::ScopedBIO bio_time(BIO_new(BIO_s_mem()));
  if (!bio_time) {
    LOG(WARNING) << "Failed to create BIO for issue date.";
    return false;
  }

  if (ASN1_TIME_print(bio_time.get(), not_before) <= 0) {
    LOG(WARNING) << "Failed to print certificate issue date.";
    return false;
  }

  char* time_str;
  int64_t len = BIO_get_mem_data(bio_time.get(), &time_str);
  if (len <= 0) {
    LOG(WARNING) << "Failed to get BIO mem data for issue date.";
    return false;
  }
  *issue_date_out = std::string(time_str, len);

  return true;
}

}  // namespace arc_attestation

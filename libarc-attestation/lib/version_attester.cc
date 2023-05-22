// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libarc-attestation/lib/version_attester.h>

#include <string>

#include <brillo/secure_blob.h>
#include <libarc_attestation/proto_bindings/arc_attestation_blob.pb.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/attestation/frontend.h>

using brillo::BlobFromString;

namespace arc_attestation {

VersionAttester::VersionAttester(Provisioner* provisioner)
    : provisioner_(provisioner),
      default_hwsec_factory_(nullptr),
      hwsec_factory_(nullptr),
      hwsec_frontend_(nullptr) {}

AndroidStatus VersionAttester::QuoteCrOSBlob(const brillo::Blob& challenge,
                                             brillo::Blob& output) {
  if (!InitHwsec()) {
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  if (!provisioner_->is_provisioned()) {
    LOG(ERROR) << "Unable to quote OS version without provisioning keys";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  std::optional<std::string> key_blob = provisioner_->GetTpmCertifyingKeyBlob();
  if (!key_blob.has_value()) {
    LOG(ERROR) << "No key blob for TpmCertifying key when quoting CrOSBlob";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }
  std::optional<std::string> cert = provisioner_->GetTpmCertifyingKeyCert();
  if (!cert.has_value()) {
    LOG(ERROR) << "No cert for TpmCertifying key when quoting CrOSBlob";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  auto result = hwsec_frontend_->AttestVersion(BlobFromString(*key_blob), *cert,
                                               challenge);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to attest OS version: " << result.status();
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  CrOSSpecificBlob result_blob;
  *result_blob.mutable_version_attestation() = result.value();

  std::string output_str;
  if (!result_blob.SerializeToString(&output_str)) {
    LOG(ERROR) << "Failure to serialize blob in VersionAttester::QuoteCrOSBlob";
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }

  output = brillo::BlobFromString(output_str);
  return AndroidStatus::ok();
}

bool VersionAttester::InitHwsec() {
  if (!hwsec_factory_) {
    if (!default_hwsec_factory_) {
      default_hwsec_factory_ = std::make_unique<hwsec::FactoryImpl>(
          hwsec::ThreadingMode::kCurrentThread);
    }
    hwsec_factory_ = default_hwsec_factory_.get();
  }
  if (!hwsec_frontend_) {
    hwsec_frontend_ = hwsec_factory_->GetArcAttestationFrontend();
  }
  return true;
}

}  // namespace arc_attestation

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_VERIFY_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_VERIFY_H_

#include <optional>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>

namespace cryptohome {

// Represents a single cert in the recoverable key store certificate list.
struct RecoverableKeyStoreCert {
  // The public key of the certificate. This must be a SecureBox-encoded EC
  // public key.
  brillo::Blob public_key;
  // TODO(b/312628857): Add certificate path as well because it needs to be set
  // in the RecoverableKeyStore proto.
};

// Represents a parsed recoverable key store certificate list.
struct RecoverableKeyStoreCertList {
  uint64_t version;
  std::vector<RecoverableKeyStoreCert> certs;
};

// Get the version of the certificate xml. This doesn't attempt to verify
// the certificate.
std::optional<uint64_t> GetCertXmlVersion(const std::string& cert_xml);

// Verify that the given certificate and certificate signature xml files are
// valid, and parse the endpoint certificates into the
// RecoverableKeyStoreCertList structure.
std::optional<RecoverableKeyStoreCertList>
VerifyAndParseRecoverableKeyStoreBackendCertXmls(
    const std::string& cert_xml, const std::string& signature_xml);

// Parse the signature xml. This is not intended to be called by others. Exposed
// only for fuzzing purposes.
struct SignatureXmlParseResult {
  std::vector<brillo::Blob> intermediate_certs;
  brillo::Blob signing_cert;
  brillo::Blob signature;
};
std::optional<SignatureXmlParseResult> ParseSignatureXml(
    const std::string& signature_xml);

// Parse the certificate xml. This is not intended to be called by others.
// Exposed only for fuzzing purposes.
struct CertificateXmlParseResult {
  uint64_t version;
  std::vector<brillo::Blob> intermediate_certs;
  std::vector<brillo::Blob> endpoint_certs;
};
std::optional<CertificateXmlParseResult> ParseCertificateXml(
    const std::string& cert_xml);

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_VERIFY_H_

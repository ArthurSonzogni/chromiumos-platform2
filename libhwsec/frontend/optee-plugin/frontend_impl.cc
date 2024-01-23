// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/optee-plugin/frontend_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <openssl/bio.h>
#include <openssl/pkcs7.h>
#include <openssl/safestack.h>
#include <openssl/x509.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/space.h"

using hwsec_foundation::status::MakeStatus;

namespace {

// Note: The stack doesn't own the elements, it just references them.
struct StackOfX509RefFree {
  void operator()(STACK_OF(X509) * ptr) const { sk_X509_free(ptr); }
};
using ScopedStackOfX509Ref =
    std::unique_ptr<STACK_OF(X509), StackOfX509RefFree>;

constexpr size_t kPemWrapSize = 76;
constexpr char kPemCertTemplate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "%s"
    "-----END CERTIFICATE-----\n";

std::string RawX509ToPEM(const brillo::Blob& x509) {
  std::string base64 = base::Base64Encode(x509);

  std::string wrapped;
  for (size_t pos = 0; pos < base64.size(); pos += kPemWrapSize) {
    wrapped += base64.substr(pos, kPemWrapSize) + "\n";
  }

  return base::StringPrintf(kPemCertTemplate, wrapped.c_str());
}

}  // namespace

namespace hwsec {

StatusOr<brillo::Blob> OpteePluginFrontendImpl::SendRawCommand(
    const brillo::Blob& command) const {
  return middleware_.CallSync<&Backend::Vendor::SendRawCommand>(command);
}

StatusOr<brillo::Blob> OpteePluginFrontendImpl::GetRootOfTrustCert() const {
  ASSIGN_OR_RETURN(bool is_ready,
                   middleware_.CallSync<&Backend::RoData::IsReady>(
                       RoSpace::kWidevineRootOfTrustCert),
                   _.WithStatus<TPMError>("NV space not ready"));
  if (!is_ready) {
    return MakeStatus<TPMError>("NV space not ready", TPMRetryAction::kNoRetry);
  }
  return middleware_.CallSync<&Backend::RoData::Read>(
      RoSpace::kWidevineRootOfTrustCert);
}

StatusOr<brillo::Blob> OpteePluginFrontendImpl::GetChipIdentifyKeyCert() const {
  ASSIGN_OR_RETURN(bool is_ready,
                   middleware_.CallSync<&Backend::RoData::IsReady>(
                       RoSpace::kChipIdentityKeyCert),
                   _.WithStatus<TPMError>("NV space not ready"));
  if (!is_ready) {
    return MakeStatus<TPMError>("NV space not ready", TPMRetryAction::kNoRetry);
  }
  return middleware_.CallSync<&Backend::RoData::Read>(
      RoSpace::kChipIdentityKeyCert);
}

StatusOr<brillo::Blob> OpteePluginFrontendImpl::GetPkcs7CertChain() const {
  ASSIGN_OR_RETURN(const brillo::Blob& cik_cert, GetChipIdentifyKeyCert(),
                   _.WithStatus<TPMError>("Failed to get CIK cert"));

  const uint8_t* cik_cert_ptr = cik_cert.data();
  crypto::ScopedX509 cik_x509(
      d2i_X509(/*px=*/nullptr, &cik_cert_ptr, cik_cert.size()));
  if (!cik_x509) {
    return MakeStatus<TPMError>("Failed to parse CIK cert",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(const brillo::Blob& rot_cert, GetRootOfTrustCert(),
                   _.WithStatus<TPMError>("Failed to get RoT cert"));

  const uint8_t* rot_cert_ptr = rot_cert.data();
  crypto::ScopedX509 rot_x509(
      d2i_X509(/*px=*/nullptr, &rot_cert_ptr, rot_cert.size()));
  if (!rot_x509) {
    return MakeStatus<TPMError>("Failed to parse RoT cert",
                                TPMRetryAction::kNoRetry);
  }

  // Put CIK and RoT certs into a STACK_OF(X509) structure.
  ScopedStackOfX509Ref x509_stack(sk_X509_new_null());
  if (!x509_stack) {
    return MakeStatus<TPMError>("Failed to allocate STACK_OF(X509) structure",
                                TPMRetryAction::kNoRetry);
  }
  if (sk_X509_push(x509_stack.get(), cik_x509.get()) < 0) {
    return MakeStatus<TPMError>("Failed to push CIK cert into STACK_OF(X509)",
                                TPMRetryAction::kNoRetry);
  }
  if (sk_X509_push(x509_stack.get(), rot_x509.get()) < 0) {
    return MakeStatus<TPMError>("Failed to push RoT cert into STACK_OF(X509)",
                                TPMRetryAction::kNoRetry);
  }

  // Empty data to sign.
  crypto::ScopedBIO bio(BIO_new(BIO_s_mem()));

  crypto::ScopedOpenSSL<PKCS7, PKCS7_free> p7(PKCS7_sign(
      nullptr, nullptr, x509_stack.get(), bio.get(), PKCS7_DETACHED));
  if (!p7) {
    return MakeStatus<TPMError>("Failed to allocate PKCS7",
                                TPMRetryAction::kNoRetry);
  }

  int p7_len = i2d_PKCS7(p7.get(), nullptr);
  if (p7_len < 0) {
    return MakeStatus<TPMError>("Failed to get pkcs7 length",
                                TPMRetryAction::kNoRetry);
  }

  brillo::Blob p7_blob(p7_len);
  uint8_t* p7_ptr = p7_blob.data();
  p7_len = i2d_PKCS7(p7.get(), &p7_ptr);
  if (p7_len != p7_blob.size()) {
    return MakeStatus<TPMError>("Mismatched pkcs7 length",
                                TPMRetryAction::kNoRetry);
  }

  return p7_blob;
}

StatusOr<std::string> OpteePluginFrontendImpl::GetPemCertChain() const {
  ASSIGN_OR_RETURN(const brillo::Blob& cik_cert, GetChipIdentifyKeyCert(),
                   _.WithStatus<TPMError>("Failed to get CIK cert"));

  ASSIGN_OR_RETURN(const brillo::Blob& rot_cert, GetRootOfTrustCert(),
                   _.WithStatus<TPMError>("Failed to get RoT cert"));

  std::string cik_pem = RawX509ToPEM(cik_cert);
  std::string rot_pem = RawX509ToPEM(rot_cert);

  return rot_pem + cik_pem;
}

}  // namespace hwsec

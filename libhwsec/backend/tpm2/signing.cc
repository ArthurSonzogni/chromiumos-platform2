// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/signing.h"

#include <limits>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_utility.h>

#include "libhwsec/backend/digest_algorithms.h"
#include "libhwsec/backend/tpm2/static_utils.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"
#include "trunks/tpm_generated.h"

using brillo::BlobFromString;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using PssParams = SigningOptions::PssParams;
using RsaPaddingScheme = SigningOptions::RsaPaddingScheme;
using EcdsaEncoding = SigningOptions::EcdsaEncoding;

namespace {

StatusOr<trunks::TPM_ALG_ID> ToTrunksDigestAlgorithm(DigestAlgorithm algo) {
  switch (algo) {
    case DigestAlgorithm::kNoDigest:
      return trunks::TPM_ALG_NULL;
    case DigestAlgorithm::kSha1:
      return trunks::TPM_ALG_SHA1;
    case DigestAlgorithm::kSha256:
      return trunks::TPM_ALG_SHA256;
    case DigestAlgorithm::kSha384:
      return trunks::TPM_ALG_SHA384;
    case DigestAlgorithm::kSha512:
      return trunks::TPM_ALG_SHA512;
    default:
      return MakeStatus<TPMError>("Unknown digest algorithm",
                                  TPMRetryAction::kNoRetry);
  }
}

StatusOr<std::string> AddPKCS1Padding(const std::string& input, size_t size) {
  if (input.size() + 11 > size) {
    return MakeStatus<TPMError>("Message too long", TPMRetryAction::kNoRetry);
  }
  std::string result("\x00\x01", 2);
  result.append(size - input.size() - 3, '\xff');
  result.append("\x00", 1);
  result.append(input);
  return result;
}

StatusOr<crypto::ScopedRSA> NumberToScopedRsa(const brillo::Blob& modulus,
                                              const brillo::Blob& exponent) {
  crypto::ScopedRSA rsa(RSA_new());
  if (!rsa) {
    return MakeStatus<TPMError>("Failed to allocate RSA",
                                TPMRetryAction::kNoRetry);
  }

  crypto::ScopedBIGNUM n(BN_new()), e(BN_new());
  if (!n || !e) {
    return MakeStatus<TPMError>("Failed to allocate BIGNUM",
                                TPMRetryAction::kNoRetry);
  }

  if (!BN_bin2bn(modulus.data(), modulus.size(), n.get()) ||
      !BN_bin2bn(exponent.data(), exponent.size(), e.get())) {
    return MakeStatus<TPMError>("Failed to convert modulus or exponent for RSA",
                                TPMRetryAction::kNoRetry);
  }

  if (!RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    return MakeStatus<TPMError>("Failed to set modulus or exponent for RSA",
                                TPMRetryAction::kNoRetry);
  }

  return rsa;
}

StatusOr<crypto::ScopedRSA> PublicAreaToScopedRsa(
    const trunks::TPMT_PUBLIC& public_area) {
  // Extract modulus and exponent from public_area.
  std::string modulus;
  std::string exponent;
  modulus.assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(public_area.unique.rsa));

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(trunks::Serialize_UINT32(
                      public_area.parameters.rsa_detail.exponent, &exponent)))
      .WithStatus<TPMError>("Error serializing public exponent");

  return NumberToScopedRsa(brillo::BlobFromString(modulus),
                           brillo::BlobFromString(exponent));
}

}  // namespace

StatusOr<brillo::Blob> SigningTpm2::Sign(Key key,
                                         const brillo::Blob& data,
                                         const SigningOptions& options) {
  ASSIGN_OR_RETURN(const brillo::Blob& hashed_data,
                   DigestData(options.digest_algorithm, data));
  return RawSign(key, hashed_data, options);
}

StatusOr<brillo::Blob> SigningTpm2::RawSign(Key key,
                                            const brillo::Blob& data,
                                            const SigningOptions& options) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, key_management_.GetKeyData(key),
                   _.WithStatus<TPMError>("Failed to get the key data"));
  const trunks::TPMT_PUBLIC& public_area = key_data.cache.public_area;

  ASSIGN_OR_RETURN(trunks::TPM_ALG_ID sign_algorithm,
                   GetSignAlgorithm(key_data, options),
                   _.WithStatus<TPMError>("Failed to get signing algorithm"));

  if (public_area.type == trunks::TPM_ALG_RSA &&
      public_area.object_attributes & trunks::kDecrypt) {
    return RawSignRsaWithDecrypt(sign_algorithm, key_data, data, options);
  }

  std::string data_to_sign = brillo::BlobToString(data);
  DigestAlgorithm digest_algorithm = options.digest_algorithm;

  if (digest_algorithm == DigestAlgorithm::kNoDigest &&
      sign_algorithm == trunks::TPM_ALG_RSASSA) {
    // Parse and remove the digest info from the input if there is no specified
    // digest algorithm.
    std::optional<ParsedDigestInfo> parsed = ParseDigestInfo(data);
    if (parsed.has_value()) {
      digest_algorithm = parsed->algorithm;
      data_to_sign = brillo::BlobToString(parsed->blob);
    }
  }

  // Detect digest algorithm based on data length. Fixes 802.1x
  // wpa_supplicant which uses RawSign with ECDSA SHA1.
  if (sign_algorithm == trunks::TPM_ALG_ECDSA &&
      digest_algorithm == DigestAlgorithm::kNoDigest) {
    switch (data.size()) {
      case GetDigestLength(DigestAlgorithm::kSha1):
        digest_algorithm = DigestAlgorithm::kSha1;
        break;
      case GetDigestLength(DigestAlgorithm::kSha224):
        digest_algorithm = DigestAlgorithm::kSha224;
        break;
      case GetDigestLength(DigestAlgorithm::kSha256):
        digest_algorithm = DigestAlgorithm::kSha256;
        break;
      case GetDigestLength(DigestAlgorithm::kSha384):
        digest_algorithm = DigestAlgorithm::kSha384;
        break;
      case GetDigestLength(DigestAlgorithm::kSha512):
        digest_algorithm = DigestAlgorithm::kSha512;
        break;
    }
  }

  trunks::TPM_ALG_ID digest_alg = trunks::TPM_ALG_NULL;

  StatusOr<trunks::TPM_ALG_ID> alg = ToTrunksDigestAlgorithm(digest_algorithm);
  if (alg.ok()) {
    digest_alg = alg.value();
    // TODO(b/229523619): Check the pss_params are valid.
  } else if (sign_algorithm == trunks::TPM_ALG_RSASSA) {
    // If TPM doesn't support the digest type (ex. MD5), we need to prepend
    // DigestInfo and then call TPM Sign with NULL scheme to sign and pad.
    ASSIGN_OR_RETURN(const brillo::Blob& der_header,
                     GetDigestAlgorithmEncoding(digest_algorithm));
    data_to_sign = brillo::BlobToString(der_header) + data_to_sign;
    digest_alg = trunks::TPM_ALG_NULL;
  } else {
    return MakeStatus<TPMError>("Unsupported digest algorithm combination")
        .Wrap(std::move(alg).err_status());
  }

  ASSIGN_OR_RETURN(
      ConfigTpm2::TrunksSession session,
      config_.GetTrunksSession(key_data.cache.policy,
                               SessionSecuritySetting::kSaltAndEncrypted),
      _.WithStatus<TPMError>("Failed to get session for policy"));

  std::string signature;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTpmUtility().Sign(
          key_data.key_handle, sign_algorithm, digest_alg, data_to_sign,
          /*generate_hash=*/false, session.delegate, &signature)))
      .WithStatus<TPMError>("Failed to sign the data");

  if (sign_algorithm == trunks::TPM_ALG_ECDSA) {
    // Transform TPM format to PKCS#11 format
    trunks::TPMT_SIGNATURE tpm_signature;

    RETURN_IF_ERROR(MakeStatus<TPM2Error>(trunks::Parse_TPMT_SIGNATURE(
                        &signature, &tpm_signature, nullptr)))
        .WithStatus<TPMError>("Failed to parse TPM signing result");

    if (tpm_signature.sig_alg != sign_algorithm) {
      return MakeStatus<TPMError>("Response signing algorithm mismatch",
                                  TPMRetryAction::kNoRetry);
    }

    brillo::Blob ecdsa_signature;
    if (options.ecdsa_encoding == EcdsaEncoding::kDer) {
      ASSIGN_OR_RETURN(const std::string& sig,
                       SerializeFromTpmSignature(tpm_signature));
      ecdsa_signature = BlobFromString(sig);
    } else {
      const auto& r = tpm_signature.signature.ecdsa.signature_r;
      const auto& s = tpm_signature.signature.ecdsa.signature_s;

      // PKCS#11 ECDSA format is the concation of r and s (r|s).
      ecdsa_signature = brillo::CombineBlobs({
          brillo::Blob(r.buffer, r.buffer + r.size),
          brillo::Blob(s.buffer, s.buffer + s.size),
      });
    }
    return ecdsa_signature;
  }

  return brillo::BlobFromString(signature);
}

StatusOr<brillo::Blob> SigningTpm2::RawSignRsaWithDecrypt(
    trunks::TPM_ALG_ID sign_algorithm,
    const KeyTpm2& key_data,
    const brillo::Blob& data_to_sign,
    const SigningOptions& options) {
  // In PKCS1.5 of RSASSA, the signed data will be
  //    <DigestInfo encoding><input><padding>
  // where <input> is usually a digest
  //
  // If decryption is allowed for the key, we will add DigestInfo and padding in
  // software. Then, perform raw RSA on TPM by sending Decrypt command with NULL
  // scheme.
  const trunks::TPMT_PUBLIC& public_area = key_data.cache.public_area;

  std::string padded_data;
  if (sign_algorithm == trunks::TPM_ALG_RSASSA) {
    ASSIGN_OR_RETURN(const brillo::Blob& der_header,
                     GetDigestAlgorithmEncoding(options.digest_algorithm));
    ASSIGN_OR_RETURN(padded_data,
                     AddPKCS1Padding(brillo::BlobToString(der_header) +
                                         brillo::BlobToString(data_to_sign),
                                     public_area.unique.rsa.size));
  } else {
    // RSASSA-PSS needs a real digest algorithm: the padding step below
    // dereferences the EVP_MD and consumes exactly hLen bytes of input.
    if (options.digest_algorithm == DigestAlgorithm::kNoDigest) {
      return MakeStatus<TPMError>("PSS signing requires a digest algorithm",
                                  TPMRetryAction::kNoRetry);
    }

    const size_t digest_length = GetDigestLength(options.digest_algorithm);

    // The input must be exactly one digest. RSA_padding_add_PKCS1_PSS_mgf1()
    // reads exactly hLen bytes, so a shorter input reads out of bounds and a
    // longer one is signed with a silently truncated message. Chaps performs
    // the same equality check on its software path; keep the two aligned.
    if (data_to_sign.size() != digest_length) {
      return MakeStatus<TPMError>("Data to sign is too small or too large",
                                  TPMRetryAction::kNoRetry);
    }

    // Guard the default salt length against unsigned underflow when the
    // modulus is too small for the digest (e.g. a 512-bit key with SHA-512).
    // An underflowed size_t narrows to the OpenSSL sentinels
    // RSA_PSS_SALTLEN_DIGEST (-1) or RSA_PSS_SALTLEN_AUTO (-2). This is a
    // conservative pre-check; OpenSSL enforces the exact emLen bound below.
    if (public_area.unique.rsa.size < digest_length + 2) {
      return MakeStatus<TPMError>("RSA key too small for PSS with this digest",
                                  TPMRetryAction::kNoRetry);
    }

    PssParams pss_params = options.pss_params.value_or(PssParams{
        .mgf1_algorithm = options.digest_algorithm,
        .salt_length = public_area.unique.rsa.size - digest_length - 2,
    });

    // PssParams::salt_length is a size_t but OpenSSL takes an int, so a
    // caller-supplied value above INT_MAX narrows to a negative number and
    // lands on the RSA_PSS_SALTLEN_* sentinels, silently signing with a salt
    // policy the caller never asked for. Bound it here, in the layer that
    // owns the conversion, rather than relying on every caller to do it.
    if (pss_params.salt_length >
        static_cast<size_t>(std::numeric_limits<int>::max())) {
      return MakeStatus<TPMError>("PSS salt length out of range",
                                  TPMRetryAction::kNoRetry);
    }

    const EVP_MD* digest = GetOpenSSLDigest(options.digest_algorithm);
    const EVP_MD* mgf1_digest = GetOpenSSLDigest(pss_params.mgf1_algorithm);
    if (digest == nullptr || mgf1_digest == nullptr) {
      return MakeStatus<TPMError>("Unsupported digest algorithm for PSS",
                                  TPMRetryAction::kNoRetry);
    }

    ASSIGN_OR_RETURN(crypto::ScopedRSA rsa, PublicAreaToScopedRsa(public_area));

    brillo::Blob padded_data_blob(RSA_size(rsa.get()));

    if (RSA_padding_add_PKCS1_PSS_mgf1(rsa.get(), padded_data_blob.data(),
                                       data_to_sign.data(), digest, mgf1_digest,
                                       pss_params.salt_length) != 1) {
      return MakeStatus<TPMError>("Failed to produce the PSA PSS paddings",
                                  TPMRetryAction::kNoRetry);
    }
    padded_data = brillo::BlobToString(padded_data_blob);
  }

  ASSIGN_OR_RETURN(
      ConfigTpm2::TrunksSession session,
      config_.GetTrunksSession(key_data.cache.policy,
                               SessionSecuritySetting::kSaltAndEncrypted),
      _.WithStatus<TPMError>("Failed to get session for policy"));

  std::string signature;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTpmUtility().AsymmetricDecrypt(
          key_data.key_handle, trunks::TPM_ALG_NULL, trunks::TPM_ALG_NULL,
          padded_data, session.delegate, &signature)))
      .WithStatus<TPMError>("Failed to sign the data wish asymmetric decrypt");
  return brillo::BlobFromString(signature);
}

Status SigningTpm2::Verify(Key key, const brillo::Blob& signed_data) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

StatusOr<trunks::TPM_ALG_ID> SigningTpm2::GetSignAlgorithm(
    const KeyTpm2& key_data, const SigningOptions& options) {
  const trunks::TPMT_PUBLIC& public_area = key_data.cache.public_area;
  switch (public_area.type) {
    case trunks::TPM_ALG_RSA:
      switch (
          options.rsa_padding_scheme.value_or(RsaPaddingScheme::kPkcs1v15)) {
        case RsaPaddingScheme::kPkcs1v15:
          return trunks::TPM_ALG_RSASSA;
        case RsaPaddingScheme::kRsassaPss:
          return trunks::TPM_ALG_RSAPSS;
      }
      break;
    case trunks::TPM_ALG_ECC:
      return trunks::TPM_ALG_ECDSA;
  }
  return MakeStatus<TPMError>("Unknown TPM key type", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec

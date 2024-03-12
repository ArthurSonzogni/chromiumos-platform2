// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/static_utils.h"

#include <string>
#include <utility>

#include <base/hash/sha1.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <libhwsec-foundation/crypto/openssl.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/error/tpm_manager_error.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

constexpr uint32_t kMaxPasswordLength = sizeof(trunks::TPMU_HA);

StatusOr<crypto::ScopedBIGNUM> StringToBignum(const std::string& big_integer) {
  if (big_integer.empty()) {
    return MakeStatus<TPMError>("Input string is empty",
                                TPMRetryAction::kNoRetry);
  }

  crypto::ScopedBIGNUM bn(BN_new());
  if (!bn) {
    return MakeStatus<TPMError>("Failed to allocate BIGNUM",
                                TPMRetryAction::kNoRetry);
  }
  if (!BN_bin2bn(reinterpret_cast<const uint8_t*>(big_integer.data()),
                 big_integer.length(), bn.get())) {
    return MakeStatus<TPMError>("Failed to convert string to BIGNUM",
                                TPMRetryAction::kNoRetry);
  }
  return bn;
}

StatusOr<crypto::ScopedECDSA_SIG> CreateEcdsaSigFromRS(const std::string& r,
                                                       const std::string& s) {
  ASSIGN_OR_RETURN(crypto::ScopedBIGNUM r_bn, StringToBignum(r));
  ASSIGN_OR_RETURN(crypto::ScopedBIGNUM s_bn, StringToBignum(s));

  crypto::ScopedECDSA_SIG sig(ECDSA_SIG_new());
  if (!sig) {
    return MakeStatus<TPMError>("Failed to allocate ECDSA",
                                TPMRetryAction::kNoRetry);
  }
  if (!ECDSA_SIG_set0(sig.get(), r_bn.release(), s_bn.release())) {
    return MakeStatus<TPMError>("Failed to set ECDSA SIG parameters",
                                TPMRetryAction::kNoRetry);
  }
  return sig;
}

}  // namespace

StatusOr<std::string> SerializeFromTpmSignature(
    const trunks::TPMT_SIGNATURE& signature) {
  switch (signature.sig_alg) {
    case trunks::TPM_ALG_RSASSA:
      if (signature.signature.rsassa.sig.size >
          sizeof(signature.signature.rsassa.sig.buffer)) {
        return MakeStatus<TPMError>("RSASSA signature overflow",
                                    TPMRetryAction::kNoRetry);
      }
      return StringFrom_TPM2B_PUBLIC_KEY_RSA(signature.signature.rsassa.sig);
    case trunks::TPM_ALG_ECDSA: {
      if (signature.signature.ecdsa.signature_r.size >
              sizeof(signature.signature.ecdsa.signature_r.buffer) ||
          signature.signature.ecdsa.signature_s.size >
              sizeof(signature.signature.ecdsa.signature_s.buffer)) {
        return MakeStatus<TPMError>("ECDSA signature overflow",
                                    TPMRetryAction::kNoRetry);
      }
      ASSIGN_OR_RETURN(
          crypto::ScopedECDSA_SIG sig,
          CreateEcdsaSigFromRS(StringFrom_TPM2B_ECC_PARAMETER(
                                   signature.signature.ecdsa.signature_r),
                               StringFrom_TPM2B_ECC_PARAMETER(
                                   signature.signature.ecdsa.signature_s)),
          _.WithStatus<TPMError>("Failed to create ECDSA SIG"));
      std::string result = hwsec_foundation::ECDSASignatureToString(sig);
      if (result.empty()) {
        return MakeStatus<TPMError>("Failed to convert ECDSA signature",
                                    TPMRetryAction::kNoRetry);
      }
      return result;
    }
    default:
      return MakeStatus<TPMError>("Unkown TPM 2.0 signature type",
                                  TPMRetryAction::kNoRetry);
  }
}

std::string GetTpm2PCRValueForMode(
    const DeviceConfigSettings::BootModeSetting::Mode& mode) {
  char boot_modes[3] = {mode.developer_mode, mode.recovery_mode,
                        mode.verified_firmware};
  std::string mode_str(std::begin(boot_modes), std::end(boot_modes));
  std::string mode_digest = base::SHA1HashString(mode_str);
  mode_digest.resize(SHA256_DIGEST_LENGTH);
  const std::string pcr_initial_value(SHA256_DIGEST_LENGTH, 0);
  return crypto::SHA256HashString(pcr_initial_value + mode_digest);
}

StatusOr<brillo::SecureBlob> GetEndorsementPassword(
    org::chromium::TpmManagerProxyInterface& tpm_manager) {
  tpm_manager::GetTpmStatusRequest status_request;
  tpm_manager::GetTpmStatusReply status_reply;
  if (brillo::ErrorPtr err; !tpm_manager.GetTpmStatus(
          status_request, &status_reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }
  RETURN_IF_ERROR(MakeStatus<TPMManagerError>(status_reply.status()));

  brillo::SecureBlob password(status_reply.local_data().endorsement_password());
  if (password.empty()) {
    return MakeStatus<TPMError>("Empty endorsement password",
                                TPMRetryAction::kLater);
  }
  if (password.size() > kMaxPasswordLength) {
    return MakeStatus<TPMError>("Endorsement password too large",
                                TPMRetryAction::kLater);
  }
  return password;
}

}  // namespace hwsec

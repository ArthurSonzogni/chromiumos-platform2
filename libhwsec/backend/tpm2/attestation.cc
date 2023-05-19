// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/attestation.h"

#include <memory>
#include <optional>
#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::status::MakeStatus;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;

namespace hwsec {

namespace {

template <typename OpenSSLType, auto openssl_func>
StatusOr<std::string> OpenSSLObjectToString(OpenSSLType* object) {
  if (object == nullptr) {
    return MakeStatus<TPMError>("Object is null", TPMRetryAction::kNoRetry);
  }

  unsigned char* openssl_buffer = nullptr;
  int size = openssl_func(object, &openssl_buffer);
  if (size < 0) {
    return MakeStatus<TPMError>("Failed to call openssl_func",
                                TPMRetryAction::kNoRetry);
  }
  crypto::ScopedOpenSSLBytes scoped_buffer(openssl_buffer);

  return std::string(openssl_buffer, openssl_buffer + size);
}

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

StatusOr<std::string> SerializeFromTpmSignature(
    const trunks::TPMT_SIGNATURE& signature) {
  switch (signature.sig_alg) {
    case trunks::TPM_ALG_RSASSA:
      return StringFrom_TPM2B_PUBLIC_KEY_RSA(signature.signature.rsassa.sig);
    case trunks::TPM_ALG_ECDSA: {
      ASSIGN_OR_RETURN(
          crypto::ScopedECDSA_SIG sig,
          CreateEcdsaSigFromRS(StringFrom_TPM2B_ECC_PARAMETER(
                                   signature.signature.ecdsa.signature_r),
                               StringFrom_TPM2B_ECC_PARAMETER(
                                   signature.signature.ecdsa.signature_s)),
          _.WithStatus<TPMError>("Failed to create ECDSA SIG"));
      return OpenSSLObjectToString<ECDSA_SIG, i2d_ECDSA_SIG>(sig.get());
    }
    default:
      return MakeStatus<TPMError>("Unkown TPM 2.0 signature type",
                                  TPMRetryAction::kNoRetry);
  }
}

}  // namespace

StatusOr<attestation::Quote> AttestationTpm2::Quote(
    DeviceConfigs device_configs, Key key) {
  if (device_configs.none()) {
    return MakeStatus<TPMError>("No device config specified",
                                TPMRetryAction::kNoRetry);
  }

  attestation::Quote quote;
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, key_management_.GetKeyData(key));
  ASSIGN_OR_RETURN(const ConfigTpm2::PcrMap& pcr_map,
                   config_.ToPcrMap(device_configs),
                   _.WithStatus<TPMError>("Failed to get PCR map"));

  if (pcr_map.size() == 1) {
    int pcr = pcr_map.begin()->first;
    ASSIGN_OR_RETURN(const std::string& value, config_.ReadPcr(pcr),
                     _.WithStatus<TPMError>("Failed to read PCR"));
    quote.set_quoted_pcr_value(value);
  }

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context_.GetTrunksFactory().GetPasswordAuthorization("");

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  ASSIGN_OR_RETURN(scheme.scheme,
                   signing_.GetSignAlgorithm(key_data, SigningOptions{}),
                   _.WithStatus<TPMError>("Failed to get signing algorithm"));

  trunks::TPML_PCR_SELECTION pcr_select;
  pcr_select.count = 1;
  ASSIGN_OR_RETURN(pcr_select.pcr_selections[0],
                   config_.ToPcrSelection(device_configs),
                   _.WithStatus<TPMError>(
                       "Failed to convert device configs to PCR selection"));

  const trunks::TPM_HANDLE& key_handle = key_data.key_handle;
  std::string key_name;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      key_handle, &key_name)))
      .WithStatus<TPMError>("Failed to get key name");

  trunks::TPM2B_ATTEST quoted_struct;
  trunks::TPMT_SIGNATURE signature;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTrunksFactory().GetTpm()->QuoteSync(
          key_handle, key_name,
          trunks::Make_TPM2B_DATA("") /* No qualifying data */, scheme,
          pcr_select, &quoted_struct, &signature, delegate.get())))
      .WithStatus<TPMError>("Failed to quote");

  if (device_configs[DeviceConfig::kDeviceModel]) {
    if (StatusOr<std::string> hwid = config_.GetHardwareID(); !hwid.ok()) {
      LOG(WARNING) << "Failed to get Hardware ID: " << hwid.status();
    } else {
      quote.set_pcr_source_hint(hwid.value());
    }
  }
  ASSIGN_OR_RETURN(const std::string& sig,
                   SerializeFromTpmSignature(signature));
  quote.set_quote(sig);
  quote.set_quoted_data(StringFrom_TPM2B_ATTEST(quoted_struct));

  return quote;
}

// TODO(b/141520502): Verify the quote against expected output.
StatusOr<bool> AttestationTpm2::IsQuoted(DeviceConfigs device_configs,
                                         const attestation::Quote& quote) {
  if (device_configs.none()) {
    return MakeStatus<TPMError>("No device config specified",
                                TPMRetryAction::kNoRetry);
  }
  if (!quote.has_quoted_data()) {
    return MakeStatus<TPMError>("Invalid attestation::Quote",
                                TPMRetryAction::kNoRetry);
  }

  std::string quoted_data = quote.quoted_data();

  trunks::TPMS_ATTEST quoted_struct;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(trunks::Parse_TPMS_ATTEST(
                      &quoted_data, &quoted_struct, nullptr)))
      .WithStatus<TPMError>("Failed to parse TPMS_ATTEST");

  if (quoted_struct.magic != trunks::TPM_GENERATED_VALUE) {
    return MakeStatus<TPMError>("Bad magic value", TPMRetryAction::kNoRetry);
  }
  if (quoted_struct.type != trunks::TPM_ST_ATTEST_QUOTE) {
    return MakeStatus<TPMError>("Not a quote", TPMRetryAction::kNoRetry);
  }

  const trunks::TPML_PCR_SELECTION& pcr_select =
      quoted_struct.attested.quote.pcr_select;
  if (pcr_select.count != 1) {
    return MakeStatus<TPMError>("Wrong number of PCR selection",
                                TPMRetryAction::kNoRetry);
  }
  const trunks::TPMS_PCR_SELECTION& pcr_selection =
      pcr_select.pcr_selections[0];

  ASSIGN_OR_RETURN(trunks::TPMS_PCR_SELECTION expected_pcr_selection,
                   config_.ToPcrSelection(device_configs),
                   _.WithStatus<TPMError>(
                       "Failed to convert device configs to PCR selection"));

  if (pcr_selection.sizeof_select != expected_pcr_selection.sizeof_select) {
    return MakeStatus<TPMError>("Size of pcr_selections mismatched",
                                TPMRetryAction::kNoRetry);
  }

  for (int i = 0; i < pcr_selection.sizeof_select; ++i) {
    if (pcr_selection.pcr_select[i] != expected_pcr_selection.pcr_select[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace hwsec

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/version_attestation.h"

#include <memory>
#include <string>
#include <utility>

#include <libhwsec-foundation/status/status_chain_macros.h>

#include "base/files/file_path.h"
#include "brillo/secure_blob.h"
#include "libhwsec/backend/tpm2/static_utils.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/device_config.h"

using hwsec_foundation::status::MakeStatus;
using std::string;

namespace hwsec {

namespace {

constexpr char kLsbReleasePath[] = "/etc/lsb-release";
constexpr char kProcCmdlinePath[] = "/proc/cmdline";

}  // namespace

StatusOr<arc_attestation::CrOSVersionAttestationBlob>
VersionAttestationTpm2::AttestVersion(Key key,
                                      const std::string& cert,
                                      const brillo::Blob& challenge) {
  arc_attestation::CrOSVersionAttestationBlob result;
  result.set_version(arc_attestation::CrOSVersionAttestationVersion::
                         CROS_BLOB_VERSION_TPM2_FORMAT_1);
  result.set_tpm_certifying_key_cert(cert);

  // Populate the lsb-release and /proc/cmdline content.
  std::string lsb_release_content;
  if (!platform_.ReadFileToString(base::FilePath(kLsbReleasePath),
                                  &lsb_release_content)) {
    return MakeStatus<TPMError>("Unable to read lsb-release",
                                TPMRetryAction::kNoRetry);
  }
  result.set_lsb_release_content(lsb_release_content);

  std::string cmdline_content;
  if (!platform_.ReadFileToString(base::FilePath(kProcCmdlinePath),
                                  &cmdline_content)) {
    return MakeStatus<TPMError>("Unable to read /proc/cmdline",
                                TPMRetryAction::kNoRetry);
  }
  result.set_kernel_cmdline_content(cmdline_content);

  // Quote the PCR.
  std::string pcr_signature, pcr_quoted;
  VersionAttestationTpm2::PcrQuoteResult quote;
  ASSIGN_OR_RETURN(quote,
                   QuotePCRInternal(key, brillo::BlobToString(challenge)));

  result.set_kernel_cmdline_quote(quote.quoted);
  result.set_kernel_cmdline_quote_signature(quote.signature);

  return result;
}

StatusOr<VersionAttestationTpm2::PcrQuoteResult>
VersionAttestationTpm2::QuotePCRInternal(Key& key,
                                         const std::string& challenge) {
  // Check that the key is ECC.
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, key_management_.GetKeyData(key));
  if (key_data.cache.public_area.type != trunks::TPM_ALG_ECC) {
    return MakeStatus<TPMError>(
        "Non-ECC key not supported for Version Attestation",
        TPMRetryAction::kNoRetry);
  }

  // Prepare the PCR selection.
  ASSIGN_OR_RETURN(
      trunks::TPMS_PCR_SELECTION pcr_selection,
      config_.ToPcrSelection(DeviceConfigs{
          DeviceConfig::kBootCmdline,
      }),
      _.WithStatus<TPMError>("Boot cmdline required for version attestation "
                             "unavailable on current device"));

  trunks::TPML_PCR_SELECTION pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0] = pcr_selection;

  // Prepare the key.
  const trunks::TPM_HANDLE& key_handle = key_data.key_handle;
  std::string key_name;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      key_handle, &key_name)))
      .WithStatus<TPMError>("Failed to get key name");

  // Prepare the delegate.
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context_.GetTrunksFactory().GetPasswordAuthorization("");

  // Prepare the scheme.
  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  scheme.scheme = trunks::TPM_ALG_ECDSA;
  trunks::TPM2B_ATTEST quoted_struct;
  trunks::TPMT_SIGNATURE signature;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTrunksFactory().GetTpm()->QuoteSync(
          key_handle, key_name,
          /*qualifying_data=*/trunks::Make_TPM2B_DATA(challenge), scheme,
          pcr_select, &quoted_struct, &signature, delegate.get())))
      .WithStatus<TPMError>("Failed to quote");

  ASSIGN_OR_RETURN(const std::string& serialized_signature,
                   SerializeFromTpmSignature(signature));
  const std::string& quoted = StringFrom_TPM2B_ATTEST(quoted_struct);
  return VersionAttestationTpm2::PcrQuoteResult{
      .signature = serialized_signature, .quoted = quoted};
}

}  // namespace hwsec

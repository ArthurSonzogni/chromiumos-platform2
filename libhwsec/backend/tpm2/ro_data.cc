// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/ro_data.h"

#include <bitset>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <trunks/multiple_authorization_delegate.h>
#include <trunks/tpm_generated.h>
extern "C" {
#include <trunks/cr50_headers/virtual_nvmem.h>
}

#include "libhwsec/backend/tpm2/static_utils.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/error/tpm_manager_error.h"
#include "libhwsec/error/tpm_nvram_error.h"
#include "libhwsec/structures/no_default_init.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

using Attributes = std::bitset<tpm_manager::NvramSpaceAttribute_ARRAYSIZE>;

struct SpaceInfo {
  NoDefault<uint32_t> index;
  NoDefault<bool> read_with_owner_auth;
  Attributes require_attributes;
  Attributes deny_attributes;
};

// Note: These bitset initialization steps would not work if we have more than
// 64 kind of attributes.
constexpr Attributes kDefaultRoRequiredAttributes =
    (1ULL << tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK) |
    (1ULL << tpm_manager::NVRAM_READ_AUTHORIZATION);

bool CheckAttributes(const Attributes& require_attributes,
                     const Attributes& deny_attributes,
                     const Attributes& attributes) {
  if ((attributes & require_attributes) != require_attributes) {
    return false;
  }

  if ((attributes & deny_attributes).any()) {
    return false;
  }

  return true;
}

StatusOr<SpaceInfo> GetSpaceInfo(RoSpace space) {
  switch (space) {
    case RoSpace::kG2fCert:
      return SpaceInfo{
          .index = VIRTUAL_NV_INDEX_G2F_CERT,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    case RoSpace::kBoardId:
      return SpaceInfo{
          .index = VIRTUAL_NV_INDEX_BOARD_ID,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    case RoSpace::kSNData:
      return SpaceInfo{
          .index = VIRTUAL_NV_INDEX_SN_DATA,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    case RoSpace::kEndorsementRsaCert:
      return SpaceInfo{
          .index = trunks::kRsaEndorsementCertificateIndex,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    case RoSpace::kRsuDeviceId:
      return SpaceInfo{
          .index = VIRTUAL_NV_INDEX_RSU_DEV_ID,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    case RoSpace::kWidevineRootOfTrustCert:
      return SpaceInfo{
          .index = 0x013fff07,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    case RoSpace::kChipIdentityKeyCert:
      return SpaceInfo{
          .index = 0x013fff08,
          .read_with_owner_auth = false,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    default:
      return MakeStatus<TPMError>("Unknown space", TPMRetryAction::kNoRetry);
  }
}

struct DetailSpaceInfo {
  uint32_t size = 0;
  Attributes attributes;
};

StatusOr<DetailSpaceInfo> GetDetailSpaceInfo(
    org::chromium::TpmNvramProxyInterface& tpm_nvram,
    const SpaceInfo& space_info) {
  DetailSpaceInfo result;

  tpm_manager::GetSpaceInfoRequest request;
  // TODO(b/284263022): just use the real address once the bug is resolved.
  request.set_index(space_info.index & ~trunks::HR_NV_INDEX);
  tpm_manager::GetSpaceInfoReply reply;

  if (brillo::ErrorPtr err; !tpm_nvram.GetSpaceInfo(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  result.size = reply.size();
  for (int i = 0; i < reply.attributes().size(); ++i) {
    result.attributes[reply.attributes(i)] = true;
  }

  return result;
}

bool IsContentUnset(const trunks::TPM2B_MAX_NV_BUFFER& nv_contents) {
  // Consider NV Content unset if it's consisted of all-0s or all-1s.
  trunks::BYTE disjunction = trunks::BYTE(0), conjunction = trunks::BYTE(255);
  for (int i = 0; i < nv_contents.size; ++i) {
    disjunction |= nv_contents.buffer[i];
    conjunction &= nv_contents.buffer[i];
  }
  return (disjunction == trunks::BYTE(0)) || (conjunction == trunks::BYTE(255));
}

Status VerifyQuotedData(const trunks::TPM2B_ATTEST& quoted_struct) {
  std::string buffer(quoted_struct.attestation_data,
                     quoted_struct.attestation_data + quoted_struct.size);
  trunks::TPMS_ATTEST value;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(
                      trunks::Parse_TPMS_ATTEST(&buffer, &value, nullptr)))
      .WithStatus<TPMError>("Failed to parse TPMS_ATTEST");

  const trunks::TPM2B_MAX_NV_BUFFER& nv_contents =
      value.attested.nv.nv_contents;
  if (nv_contents.size > sizeof(nv_contents.buffer)) {
    return MakeStatus<TPMError>(
        base::StringPrintf("NV Content size is too large: %u",
                           static_cast<unsigned int>(nv_contents.size)),
        TPMRetryAction::kNoRetry);
  }
  if (nv_contents.size == trunks::UINT16(0)) {
    return MakeStatus<TPMError>("NV Content size is zero",
                                TPMRetryAction::kNoRetry);
  }
  if (IsContentUnset(nv_contents)) {
    return MakeStatus<TPMError>("NV Content unset", TPMRetryAction::kNoRetry);
  }
  return OkStatus();
}

}  // namespace

StatusOr<bool> RoDataTpm2::IsReady(RoSpace space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  StatusOr<DetailSpaceInfo> detail_info =
      GetDetailSpaceInfo(tpm_nvram_, space_info);

  if (!detail_info.ok() &&
      detail_info.err_status()->UnifiedErrorCode() ==
          TPMNvramError(
              tpm_manager::NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST)
              .UnifiedErrorCode()) {
    return false;
  }
  if (!detail_info.ok()) {
    return MakeStatus<TPMError>("Failed to get detail space info")
        .Wrap(std::move(detail_info).err_status());
  }
  return CheckAttributes(space_info.require_attributes,
                         space_info.deny_attributes, detail_info->attributes);
}

StatusOr<brillo::Blob> RoDataTpm2::Read(RoSpace space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  tpm_manager::ReadSpaceRequest request;
  // TODO(b/284263022): just use the real address once the bug is resolved.
  request.set_index(space_info.index & ~trunks::HR_NV_INDEX);
  request.set_use_owner_authorization(space_info.read_with_owner_auth);
  tpm_manager::ReadSpaceReply reply;

  if (brillo::ErrorPtr err; !tpm_nvram_.ReadSpace(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  return brillo::BlobFromString(reply.data());
}

StatusOr<attestation::Quote> RoDataTpm2::Certify(RoSpace space, Key key) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));
  ASSIGN_OR_RETURN(const DetailSpaceInfo& detail_info,
                   GetDetailSpaceInfo(tpm_nvram_, space_info),
                   _.WithStatus<TPMError>("Failed to get detail space info"));

  return CertifyWithSize(space, key, detail_info.size);
}

StatusOr<attestation::Quote> RoDataTpm2::CertifyWithSize(RoSpace space,
                                                         Key key,
                                                         int size) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));
  std::unique_ptr<trunks::AuthorizationDelegate> empty_password_authorization =
      context_.GetTrunksFactory().GetPasswordAuthorization("");

  trunks::MultipleAuthorizations authorization;
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());

  ASSIGN_OR_RETURN(const KeyTpm2& key_data, key_management_.GetKeyData(key));
  const trunks::TPM_HANDLE& key_handle = key_data.key_handle;
  std::string key_name;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      key_handle, &key_name)))
      .WithStatus<TPMError>("Failed to get key name");

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  ASSIGN_OR_RETURN(scheme.scheme,
                   signing_.GetSignAlgorithm(key_data, SigningOptions{}),
                   _.WithStatus<TPMError>("Failed to get signing algorithm"));

  trunks::TPM2B_ATTEST quoted_struct;
  trunks::TPMT_SIGNATURE signature;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(
                      context_.GetTrunksFactory().GetTpm()->NV_CertifySync(
                          key_handle,                   // sign_handle
                          key_name,                     // sign_handle_name
                          space_info.index,             // auth_handle
                          "",                           // auth_handle_name
                          space_info.index,             // nv_index
                          "",                           // nv_index_name
                          trunks::Make_TPM2B_DATA(""),  // qualifying data
                          scheme,                       // in_scheme
                          size,                         // size to read
                          0,                            // offset
                          &quoted_struct, &signature, &authorization)))
      .WithStatus<TPMError>("Failed to certify the NVs");

  // Verifies the quoted data to prevent quoting quoted_data with
  // invalid/unset/empty nvram content
  RETURN_IF_ERROR(VerifyQuotedData(quoted_struct))
      .WithStatus<TPMError>("Inavlid quoted data.");

  ASSIGN_OR_RETURN(const std::string& sig,
                   SerializeFromTpmSignature(signature));

  attestation::Quote quote;
  quote.set_quote(sig);
  quote.set_quoted_data(StringFrom_TPM2B_ATTEST(quoted_struct));

  return quote;
}

}  // namespace hwsec

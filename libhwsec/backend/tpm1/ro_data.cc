// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/ro_data.h"

#include <bitset>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trousers/tss.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "libhwsec/error/tpm_manager_error.h"
#include "libhwsec/error/tpm_nvram_error.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/no_default_init.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

using Attributes = std::bitset<tpm_manager::NvramSpaceAttribute_ARRAYSIZE>;

struct SpaceInfo {
  NoDefault<uint32_t> index;
  NoDefault<bool> read_with_owner_auth;
  NoDefault<bool> extract_x509_cert;
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
    case RoSpace::kEndorsementRsaCert:
      return SpaceInfo{
          .index = TSS_NV_DEFINED | TPM_NV_INDEX_EKCert,
          .read_with_owner_auth = true,
          .extract_x509_cert = true,
          .require_attributes = kDefaultRoRequiredAttributes,
      };
    default:
      return MakeStatus<TPMError>("Unknown space",
                                  TPMRetryAction::kSpaceNotFound);
  }
}

struct DetailSpaceInfo {
  uint32_t full_size = 0;
  Attributes attributes;
};

StatusOr<DetailSpaceInfo> GetDetailSpaceInfo(
    org::chromium::TpmNvramProxyInterface& tpm_nvram,
    const SpaceInfo& space_info) {
  DetailSpaceInfo result;

  tpm_manager::GetSpaceInfoRequest request;
  request.set_index(space_info.index);
  tpm_manager::GetSpaceInfoReply reply;

  if (brillo::ErrorPtr err; !tpm_nvram.GetSpaceInfo(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  result.full_size = reply.size();
  for (int i = 0; i < reply.attributes().size(); ++i) {
    result.attributes[reply.attributes(i)] = true;
  }

  return result;
}

StatusOr<brillo::Blob> ExtractCert(const std::string& cert_data) {
  // Verify the contents of the data and extract the X.509 certificate.
  // We are expecting data in the form of a TCG_PCCLIENT_STORED_CERT with an
  // embedded TCG_FULL_CERT. Details can be found in the TCG PC Specific
  // Implementation Specification v1.21 section 7.4.
  // | stored cert header | full cert length | full cert header | cert |
  // | 3 Bytes            | 2 Bytes          | 2 bytes          |
  //                                         | full cert length        |
  constexpr uint8_t kStoredCertHeader[] = {0x10, 0x01, 0x00};
  constexpr uint8_t kFullCertHeader[] = {0x10, 0x02};
  constexpr size_t kTotalHeaderBytes = 7;
  constexpr size_t kStoredCertHeaderOffset = 0;
  constexpr size_t kFullCertLengthOffset = 3;
  constexpr size_t kFullCertHeaderOffset = 5;
  if (cert_data.size() < kTotalHeaderBytes) {
    return MakeStatus<TPMError>("Bad header", TPMRetryAction::kNoRetry);
  }
  if (memcmp(kStoredCertHeader, &cert_data[kStoredCertHeaderOffset],
             std::size(kStoredCertHeader)) != 0) {
    return MakeStatus<TPMError>("Bad PCCLIENT_STORED_CERT",
                                TPMRetryAction::kNoRetry);
  }
  if (memcmp(kFullCertHeader, &cert_data[kFullCertHeaderOffset],
             std::size(kFullCertHeader)) != 0) {
    return MakeStatus<TPMError>("Bad PCCLIENT_FULL_CERT",
                                TPMRetryAction::kNoRetry);
  }
  size_t full_cert_size =
      (static_cast<uint8_t>(cert_data[kFullCertLengthOffset]) << 8) |
      static_cast<uint8_t>(cert_data[kFullCertLengthOffset + 1]);
  size_t full_cert_end = full_cert_size + kFullCertHeaderOffset;
  if (full_cert_end > cert_data.size()) {
    return MakeStatus<TPMError>("Bad cert size", TPMRetryAction::kNoRetry);
  }
  return brillo::Blob(cert_data.data() + kTotalHeaderBytes,
                      cert_data.data() + full_cert_end);
}

}  // namespace

StatusOr<bool> RoDataTpm1::IsReady(RoSpace space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  StatusOr<DetailSpaceInfo> detail_info =
      GetDetailSpaceInfo(tpm_nvram_, space_info);

  if (!detail_info.ok() && detail_info.err_status()->ToTPMRetryAction() ==
                               TPMRetryAction::kSpaceNotFound) {
    return false;
  }
  if (!detail_info.ok()) {
    return MakeStatus<TPMError>("Failed to get detail space info")
        .Wrap(std::move(detail_info).err_status());
  }
  return CheckAttributes(space_info.require_attributes,
                         space_info.deny_attributes, detail_info->attributes);
}

StatusOr<brillo::Blob> RoDataTpm1::Read(RoSpace space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  tpm_manager::ReadSpaceRequest request;
  request.set_index(space_info.index);
  request.set_use_owner_authorization(space_info.read_with_owner_auth);
  tpm_manager::ReadSpaceReply reply;

  if (brillo::ErrorPtr err; !tpm_nvram_.ReadSpace(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  return space_info.extract_x509_cert ? ExtractCert(reply.data())
                                      : brillo::BlobFromString(reply.data());
}

StatusOr<attestation::Quote> RoDataTpm1::Certify(RoSpace space, Key key) {
  return MakeStatus<TPMError>("Not implemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec

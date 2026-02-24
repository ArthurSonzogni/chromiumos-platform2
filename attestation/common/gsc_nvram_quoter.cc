// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/gsc_nvram_quoter.h"

#include <string>
#include <vector>

#include <base/check_op.h>
#include <base/logging.h>
#include <libhwsec/frontend/attestation/frontend.h>
#include <libhwsec/structures/space.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_utility.h>

using brillo::BlobFromString;
using hwsec::TPMError;

namespace attestation {

namespace {

struct NvramQuoteMetadata {
  NVRAMQuoteType type;
  const char* name;
  hwsec::RoSpace space;
};

constexpr NvramQuoteMetadata kNvramQuoteMetadata[] = {
    {BOARD_ID, "BOARD_ID", hwsec::RoSpace::kBoardId},
    {SN_BITS, "SN_BITS", hwsec::RoSpace::kSNData},
    {RSA_PUB_EK_CERT, "RSA_PUB_EK_CERT", hwsec::RoSpace::kEndorsementRsaCert},
    {RSU_DEVICE_ID, "RSU_DEVICE_ID", hwsec::RoSpace::kRsuDeviceId},
    {RMA_BYTES, "RMA_BYTES", hwsec::RoSpace::kRmaBytes},
    {G2F_CERT, "G2F_CERT", hwsec::RoSpace::kG2fCert},
};

constexpr bool VerifyMedataListOrder() {
  for (int i = 0; i < std::size(kNvramQuoteMetadata); ++i) {
    if (i != static_cast<int>(kNvramQuoteMetadata[i].type)) {
      return false;
    }
  }
  return true;
}

static_assert(VerifyMedataListOrder(),
              "List order should be aligned with enum in protobuf message");

}  // namespace

GscNvramQuoter::GscNvramQuoter(const hwsec::AttestationFrontend& hwsec)
    : hwsec_(hwsec) {}

std::vector<NVRAMQuoteType> GscNvramQuoter::GetListForIdentity() const {
  return {BOARD_ID, SN_BITS};
}

std::vector<NVRAMQuoteType> GscNvramQuoter::GetListForVtpmEkCertificate()
    const {
  return {SN_BITS};
}

std::vector<NVRAMQuoteType> GscNvramQuoter::GetListForEnrollmentCertificate()
    const {
  return {BOARD_ID, SN_BITS, RSU_DEVICE_ID};
}

bool GscNvramQuoter::Certify(NVRAMQuoteType type,
                             const std::string& signing_key_blob,
                             Quote& quote) {
  CHECK_LT(static_cast<uint32_t>(type), std::size(kNvramQuoteMetadata))
      << "Unexpected type: " << static_cast<uint32_t>(type) << ".";

  NvramQuoteMetadata metadata =
      kNvramQuoteMetadata[static_cast<uint32_t>(type)];

  ASSIGN_OR_RETURN(
      quote, hwsec_.CertifyNV(metadata.space, BlobFromString(signing_key_blob)),
      _.WithStatus<TPMError>(
           base::StringPrintf("Failed to certify %s", metadata.name))
          .LogError()
          .As(false));
  return true;
}

}  // namespace attestation

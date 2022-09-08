// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_COMMON_NVRAM_INDEX_PLACEHOLDER_H_
#define ATTESTATION_COMMON_NVRAM_INDEX_PLACEHOLDER_H_

// Defines the nvram spaces that is specific to GSC case.
// The purpose is to unify the variables for TPM versions, so the attestation
// service can be implemented in a more general way.
//
// See src/platform/cr50/board/cr50/tpm2/virtual_nvmem.h for reference.

namespace attestation {

enum virtual_nv_index {
  VIRTUAL_NV_INDEX_START = 0x013fff00,
  VIRTUAL_NV_INDEX_BOARD_ID = VIRTUAL_NV_INDEX_START,
  VIRTUAL_NV_INDEX_SN_DATA,
  VIRTUAL_NV_INDEX_G2F_CERT,
  VIRTUAL_NV_INDEX_RSU_DEV_ID,
  VIRTUAL_NV_INDEX_END,
};
constexpr uint32_t VIRTUAL_NV_INDEX_MAX = 0x013fffff;

constexpr uint32_t VIRTUAL_NV_INDEX_BOARD_ID_SIZE = 12;
constexpr uint32_t VIRTUAL_NV_INDEX_SN_DATA_SIZE = 16;
constexpr uint32_t VIRTUAL_NV_INDEX_G2F_CERT_SIZ = 315;
constexpr uint32_t VIRTUAL_NV_INDEX_RSU_DEV_ID_SIZE = 32;

}  // namespace attestation

#endif  // ATTESTATION_COMMON_NVRAM_INDEX_PLACEHOLDER_H_

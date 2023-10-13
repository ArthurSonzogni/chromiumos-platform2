// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h"

#include <vector>

#include <openssl/sha.h>
#include <vboot/tlcl.h>

namespace hwsec_foundation {

uint32_t TlclWrapperImpl::Init() {
#if USE_TPM_DYNAMIC
  // tlcl doesn't support TPM dynamic.
  return 1;
#else
  return TlclLibInit();
#endif
}

uint32_t TlclWrapperImpl::Close() {
  return TlclLibClose();
}

uint32_t TlclWrapperImpl::Extend(int pcr_num,
                                 const std::vector<uint8_t> in_digest,
                                 std::vector<uint8_t>* out_digest) {
  unsigned char out_buffer[TPM_PCR_DIGEST];
  memset(out_buffer, 0, TPM_PCR_DIGEST);
  uint32_t result = TlclExtend(pcr_num, in_digest.data(), out_buffer);
  if (out_digest) {
    *out_digest = std::vector<uint8_t>(out_buffer, out_buffer + TPM_PCR_DIGEST);
  }
  return result;
}

}  // namespace hwsec_foundation

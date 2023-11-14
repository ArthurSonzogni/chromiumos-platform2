// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h"

#include <brillo/secure_blob.h>
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
                                 const brillo::Blob& in_digest,
                                 brillo::Blob* out_digest) {
  uint8_t out_buffer[TPM_PCR_DIGEST];
  memset(out_buffer, 0, TPM_PCR_DIGEST);
  uint32_t result = TlclExtend(pcr_num, in_digest.data(), out_buffer);
  if (out_digest) {
    *out_digest = brillo::Blob(out_buffer, out_buffer + TPM_PCR_DIGEST);
  }
  return result;
}

uint32_t TlclWrapperImpl::GetOwnership(bool* owned) {
  uint8_t owned_out = 0;
  uint32_t result = TlclGetOwnership(&owned_out);
  if (owned) {
    *owned = static_cast<bool>(owned_out);
  }
  return result;
}

}  // namespace hwsec_foundation

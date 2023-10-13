// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_H_
#define LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_H_

#include <stdint.h>
#include <vector>

#include "libhwsec-foundation/hwsec-foundation_export.h"

namespace hwsec_foundation {

// TlclWrapper is a simple wrapper around the vboot tlcl library so that we can
// mock TPM access.
class HWSEC_FOUNDATION_EXPORT TlclWrapper {
 public:
  TlclWrapper() = default;

  virtual ~TlclWrapper() = default;

  // Initialize the Tlcl library, return 0 iff successful.
  virtual uint32_t Init() = 0;

  // Shutdown the Tlcl library, return 0 iff successful.
  virtual uint32_t Close() = 0;

  // Extend the PCR |pcr_num| with |in_digest|.
  virtual uint32_t Extend(int pcr_num,
                          const std::vector<uint8_t> in_digest,
                          std::vector<uint8_t>* out_digest) = 0;
};

// TlclWrapper is a simple wrapper around the vboot tlcl library so that we can
// mock TPM access.
class HWSEC_FOUNDATION_EXPORT TlclWrapperImpl : public TlclWrapper {
 public:
  TlclWrapperImpl() = default;
  ~TlclWrapperImpl() override = default;

  uint32_t Init() override;
  uint32_t Close() override;
  uint32_t Extend(int pcr_num,
                  const std::vector<uint8_t> in_digest,
                  std::vector<uint8_t>* out_digest) override;
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_H_

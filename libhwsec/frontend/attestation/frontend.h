// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_
#define LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"

namespace hwsec {

class AttestationFrontend : public Frontend {
 public:
  ~AttestationFrontend() override = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_

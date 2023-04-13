// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_IMPL_H_

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/attestation/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/status.h"

namespace hwsec {

class AttestationFrontendImpl : public AttestationFrontend,
                                public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~AttestationFrontendImpl() override = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_IMPL_H_

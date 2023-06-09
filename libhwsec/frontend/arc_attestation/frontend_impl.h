// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ARC_ATTESTATION_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_ARC_ATTESTATION_FRONTEND_IMPL_H_

#include <string>

#include <brillo/secure_blob.h>

#include "libarc_attestation/proto_bindings/arc_attestation_blob.pb.h"
#include "libhwsec/frontend/arc_attestation/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class ArcAttestationFrontendImpl : public ArcAttestationFrontend,
                                   public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~ArcAttestationFrontendImpl() override = default;

  StatusOr<arc_attestation::CrOSVersionAttestationBlob> AttestVersion(
      const brillo::Blob& key_blob,
      const std::string& cert,
      const brillo::Blob& challenge) const override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ARC_ATTESTATION_FRONTEND_IMPL_H_

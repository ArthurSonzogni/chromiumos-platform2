// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/arc_attestation/frontend_impl.h"

#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libarc_attestation/proto_bindings/arc_attestation_blob.pb.h"
#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

using arc_attestation::CrOSVersionAttestationBlob;

namespace hwsec {

StatusOr<CrOSVersionAttestationBlob> ArcAttestationFrontendImpl::AttestVersion(
    const brillo::Blob& key_blob,
    const std::string& cert,
    const brillo::Blob& challenge) const {
  ASSIGN_OR_RETURN(
      ScopedKey key,
      middleware_.CallSync<&Backend::KeyManagement::LoadKey>(
          OperationPolicy{}, key_blob,
          Backend::KeyManagement::LoadKeyOptions{.auto_reload = true}));

  return middleware_.CallSync<&Backend::VersionAttestation::AttestVersion>(
      key.GetKey(), cert, challenge);
}

}  // namespace hwsec

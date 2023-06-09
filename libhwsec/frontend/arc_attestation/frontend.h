// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ARC_ATTESTATION_FRONTEND_H_
#define LIBHWSEC_FRONTEND_ARC_ATTESTATION_FRONTEND_H_

#include <string>

#include "brillo/secure_blob.h"
#include "libarc_attestation/proto_bindings/arc_attestation_blob.pb.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class ArcAttestationFrontend : public Frontend {
 public:
  ~ArcAttestationFrontend() override = default;

  // Quote the ChromeOS version that is attested by the TPM.
  // |key_blob| is the key to use for quotation, |cert| is the certificate for
  // the key used for quotation, and will be embedded into the result, and
  // |challenge| is a challenge embedded into the result to prevent replay.
  virtual StatusOr<arc_attestation::CrOSVersionAttestationBlob> AttestVersion(
      const brillo::Blob& key_blob,
      const std::string& cert,
      const brillo::Blob& challenge) const = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ARC_ATTESTATION_FRONTEND_H_

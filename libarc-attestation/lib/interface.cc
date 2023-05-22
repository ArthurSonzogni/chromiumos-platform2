// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libarc-attestation/lib/interface.h>

#include <memory>
#include <utility>

#include <base/logging.h>

#include <libarc-attestation/lib/manager.h>

namespace arc_attestation {

AndroidStatus ProvisionDkCert(bool blocking) {
  return ArcAttestationManagerSingleton::Get()->manager()->ProvisionDkCert(
      blocking);
}

AndroidStatus GetDkCertChain(std::vector<brillo::Blob>& cert_out) {
  return ArcAttestationManagerSingleton::Get()->manager()->GetDkCertChain(
      cert_out);
}

AndroidStatus SignWithP256Dk(const brillo::Blob& input,
                             brillo::Blob& signature) {
  return ArcAttestationManagerSingleton::Get()->manager()->SignWithP256Dk(
      input, signature);
}

AndroidStatus QuoteCrOSBlob(const brillo::Blob& challenge,
                            brillo::Blob& output) {
  return ArcAttestationManagerSingleton::Get()->manager()->QuoteCrOSBlob(
      challenge, output);
}

}  // namespace arc_attestation

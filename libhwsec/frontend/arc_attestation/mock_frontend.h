// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ARC_ATTESTATION_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_ARC_ATTESTATION_MOCK_FRONTEND_H_

#include <string>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libarc_attestation/proto_bindings/arc_attestation_blob.pb.h"
#include "libhwsec/frontend/arc_attestation/frontend.h"
#include "libhwsec/frontend/mock_frontend.h"
#include "libhwsec/status.h"

namespace hwsec {

class MockArcAttestationFrontend : public MockFrontend,
                                   public ArcAttestationFrontend {
 public:
  MockArcAttestationFrontend() = default;
  ~MockArcAttestationFrontend() override = default;

  MOCK_METHOD(StatusOr<arc_attestation::CrOSVersionAttestationBlob>,
              AttestVersion,
              (const brillo::Blob& key_blob,
               const std::string& cert,
               const brillo::Blob& challenge),
              (const override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ARC_ATTESTATION_MOCK_FRONTEND_H_

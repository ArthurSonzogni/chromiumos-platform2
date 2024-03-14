// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBARC_ATTESTATION_LIB_TEST_UTILS_H_
#define LIBARC_ATTESTATION_LIB_TEST_UTILS_H_

#include <vector>

#include <gmock/gmock.h>

#include <libarc-attestation/lib/manager_base.h>

namespace arc_attestation {

class MockArcAttestationManager : public ArcAttestationManagerBase {
 public:
  MockArcAttestationManager() = default;

  MockArcAttestationManager(const MockArcAttestationManager&) = delete;
  MockArcAttestationManager& operator=(const MockArcAttestationManager&) =
      delete;

  MOCK_METHOD(void, Setup, (), (override));

  MOCK_METHOD(AndroidStatus, ProvisionDkCert, (bool), (override));

  MOCK_METHOD(AndroidStatus,
              GetDkCertChain,
              (std::vector<brillo::Blob>&),
              (override));

  MOCK_METHOD(AndroidStatus,
              SignWithP256Dk,
              (const brillo::Blob&, brillo::Blob&),
              (override));

  MOCK_METHOD(AndroidStatus,
              QuoteCrOSBlob,
              (const brillo::Blob&, brillo::Blob&));
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_TEST_UTILS_H_

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_U2F_H_
#define LIBHWSEC_BACKEND_MOCK_U2F_H_

#include <cstdint>
#include <optional>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/u2f.h"
#include "libhwsec/status.h"

namespace hwsec {

class MockU2f : public U2f {
 public:
  MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (override));
  MOCK_METHOD(StatusOr<u2f::GenerateResult>,
              GenerateUserPresenceOnly,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               u2f::ConsumeMode,
               u2f::UserPresenceMode),
              (override));
  MOCK_METHOD(StatusOr<u2f::GenerateResult>,
              Generate,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               u2f::ConsumeMode,
               u2f::UserPresenceMode,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<u2f::Signature>,
              SignUserPresenceOnly,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               const brillo::Blob&,
               u2f::ConsumeMode,
               u2f::UserPresenceMode,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<u2f::Signature>,
              Sign,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               const std::optional<brillo::SecureBlob>&,
               const brillo::Blob&,
               u2f::ConsumeMode,
               u2f::UserPresenceMode,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(Status,
              CheckUserPresenceOnly,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(Status,
              Check,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<u2f::Signature>,
              G2fAttest,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               const brillo::Blob&,
               const brillo::Blob&,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<u2f::Signature>,
              CorpAttest,
              (const brillo::Blob&,
               const brillo::SecureBlob&,
               const brillo::Blob&,
               const brillo::Blob&,
               const brillo::Blob&,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              GetG2fAttestData,
              (const brillo::Blob&,
               const brillo::Blob&,
               const brillo::Blob&,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<u2f::Config>, GetConfig, (), (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_U2F_H_

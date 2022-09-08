// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_MOCK_FACE_AUTHENTICATION_SESSION_DELEGATE_H_
#define FACED_MOCK_FACE_AUTHENTICATION_SESSION_DELEGATE_H_

#include <gmock/gmock.h>

#include "faced/mojom/face_auth.mojom.h"

namespace faced {

class MockFaceAuthenticationSessionDelegate
    : public chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate {
 public:
  MOCK_METHOD(
      void,
      OnAuthenticationUpdate,
      (chromeos::face_auth::mojom::AuthenticationUpdateMessagePtr message),
      (override));

  MOCK_METHOD(
      void,
      OnAuthenticationComplete,
      (chromeos::face_auth::mojom::AuthenticationCompleteMessagePtr message),
      (override));

  MOCK_METHOD(void, OnAuthenticationCancelled, (), (override));
  MOCK_METHOD(void,
              OnAuthenticationError,
              (chromeos::face_auth::mojom::SessionError error),
              (override));
};

}  // namespace faced

#endif  // FACED_MOCK_FACE_AUTHENTICATION_SESSION_DELEGATE_H_

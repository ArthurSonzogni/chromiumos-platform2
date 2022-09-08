// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_MOCK_FACE_ENROLLMENT_SESSION_DELEGATE_H_
#define FACED_MOCK_FACE_ENROLLMENT_SESSION_DELEGATE_H_

#include <gmock/gmock.h>

#include "faced/mojom/face_auth.mojom.h"

namespace faced {

class MockFaceEnrollmentSessionDelegate
    : public chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate {
 public:
  MOCK_METHOD(void,
              OnEnrollmentUpdate,
              (chromeos::face_auth::mojom::EnrollmentUpdateMessagePtr message),
              (override));

  MOCK_METHOD(
      void,
      OnEnrollmentComplete,
      (chromeos::face_auth::mojom::EnrollmentCompleteMessagePtr message),
      (override));

  MOCK_METHOD(void, OnEnrollmentCancelled, (), (override));
  MOCK_METHOD(void,
              OnEnrollmentError,
              (chromeos::face_auth::mojom::SessionError error),
              (override));
};

}  // namespace faced

#endif  // FACED_MOCK_FACE_ENROLLMENT_SESSION_DELEGATE_H_

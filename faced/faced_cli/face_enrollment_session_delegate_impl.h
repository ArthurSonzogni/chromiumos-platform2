// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACED_CLI_FACE_ENROLLMENT_SESSION_DELEGATE_IMPL_H_
#define FACED_FACED_CLI_FACE_ENROLLMENT_SESSION_DELEGATE_IMPL_H_

#include "faced/mojom/faceauth.mojom.h"

namespace faced {

class FaceEnrollmentSessionDelegateImpl
    : public chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate {
 public:
  // `FaceEnrollmentSessionDelegate` implementations
  void OnEnrollmentUpdate(
      chromeos::faceauth::mojom::EnrollmentUpdateMessagePtr message) override;
  void OnEnrollmentComplete(
      chromeos::faceauth::mojom::EnrollmentCompleteMessagePtr message) override;
  void OnEnrollmentCancelled() override;
  void OnEnrollmentError(
      chromeos::faceauth::mojom::SessionError error) override;
};

}  // namespace faced

#endif  // FACED_FACED_CLI_FACE_ENROLLMENT_SESSION_DELEGATE_IMPL_H_

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/face_enrollment_session_delegate_impl.h"

#include "faced/mojom/faceauth.mojom.h"

namespace faced {

using ::chromeos::faceauth::mojom::EnrollmentCompleteMessagePtr;
using ::chromeos::faceauth::mojom::EnrollmentUpdateMessagePtr;
using ::chromeos::faceauth::mojom::SessionError;

void FaceEnrollmentSessionDelegateImpl::OnEnrollmentUpdate(
    EnrollmentUpdateMessagePtr message) {
  // TODO(b/247034576): Handle enrollment
}

void FaceEnrollmentSessionDelegateImpl::OnEnrollmentComplete(
    EnrollmentCompleteMessagePtr message) {
  // TODO(b/247034576): Handle enrollment
}

void FaceEnrollmentSessionDelegateImpl::OnEnrollmentCancelled() {
  // TODO(b/247034576): Handle enrollment
}

void FaceEnrollmentSessionDelegateImpl::OnEnrollmentError(SessionError error) {
  // TODO(b/247034576): Handle enrollment
}

}  // namespace faced

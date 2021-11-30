// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_signature_sealing_backend.h"

#include <libhwsec-foundation/error/testing_helper.h>

using brillo::Blob;
using ::hwsec::TPMError;
using ::hwsec::TPMRetryAction;
using ::hwsec_foundation::error::testing::ReturnError;
using testing::_;

namespace cryptohome {

MockSignatureSealingBackend::MockSignatureSealingBackend() {
  ON_CALL(*this, CreateUnsealingSession(_, _, _, _, _, _, _, _))
      .WillByDefault(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));
}

MockSignatureSealingBackend::~MockSignatureSealingBackend() = default;

MockUnsealingSession::MockUnsealingSession() = default;

MockUnsealingSession::~MockUnsealingSession() = default;

}  // namespace cryptohome

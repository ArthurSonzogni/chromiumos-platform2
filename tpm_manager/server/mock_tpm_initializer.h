// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_MOCK_TPM_INITIALIZER_H_
#define TPM_MANAGER_SERVER_MOCK_TPM_INITIALIZER_H_

#include "tpm_manager/server/tpm_initializer.h"

#include <gmock/gmock.h>

namespace tpm_manager {

class MockTpmInitializer : public TpmInitializer {
 public:
  MockTpmInitializer();
  ~MockTpmInitializer() override;

  MOCK_METHOD0(InitializeTpm, bool());
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_MOCK_TPM_INITIALIZER_H_

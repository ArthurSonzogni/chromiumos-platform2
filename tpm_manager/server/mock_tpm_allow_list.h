// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_MOCK_TPM_ALLOW_LIST_H_
#define TPM_MANAGER_SERVER_MOCK_TPM_ALLOW_LIST_H_

#include "tpm_manager/server/tpm_allow_list.h"

#include <gmock/gmock.h>

namespace tpm_manager {

class MockTpmAllowList : public TpmAllowList {
 public:
  MockTpmAllowList() = default;
  ~MockTpmAllowList() override = default;

  MOCK_METHOD(bool, IsAllowed, (), (override));
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_MOCK_TPM_ALLOW_LIST_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_ALLOW_LIST_H_
#define TPM_MANAGER_SERVER_TPM_ALLOW_LIST_H_

namespace tpm_manager {

class TpmAllowList {
 public:
  TpmAllowList() = default;
  virtual ~TpmAllowList() = default;
  virtual bool IsAllowed() = 0;
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_ALLOW_LIST_H_

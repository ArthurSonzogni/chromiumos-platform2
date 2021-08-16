// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_ALLOW_LIST_IMPL_H_
#define TPM_MANAGER_SERVER_TPM_ALLOW_LIST_IMPL_H_

#include "tpm_manager/server/tpm_allow_list.h"
#include "tpm_manager/server/tpm_status.h"

namespace tpm_manager {

class TpmAllowListImpl : public TpmAllowList {
 public:
  explicit TpmAllowListImpl(TpmStatus* tpm_status);
  ~TpmAllowListImpl() override = default;
  bool IsAllowed() override;

 private:
  TpmStatus* tpm_status_ = nullptr;
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_ALLOW_LIST_IMPL_H_

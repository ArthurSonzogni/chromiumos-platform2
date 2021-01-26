// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_NEW_IMPL_H_
#define CRYPTOHOME_TPM_NEW_IMPL_H_

#include <memory>

#include <base/optional.h>
#include <tpm_manager/client/tpm_manager_utility.h>

#include "cryptohome/tpm_impl.h"

// This class should be squashed into |TpmImpl| now that the transition from
// monilithic mode to distributed mode is done.
//
// TODO(b/169388941): Remove this class after merge it back.

namespace cryptohome {

// |TpmNewImpl| is derived from |TpmImpl| and refines a set of interfaces with
// the data coming from |tpm_managerd|. In particular, the logic which should
// belong to only |tpm_managerd| (e.g. the ownership operation, owner password,
// etc.) are overwritted in this class and the corresponding setters should take
// no effect. Once |ServiceMonolithic| is obsoleted, the implementation of this
// class should be backported to |TpmImpl| and this class should be removed at
// the same time.
class TpmNewImpl : public TpmImpl {
 public:
  TpmNewImpl() = default;
  virtual ~TpmNewImpl() = default;

 private:
  // wrapped tpm_manager proxy to get information from |tpm_manager|.
  tpm_manager::TpmManagerUtility* tpm_manager_utility_{nullptr};

  // The following fields are for testing purpose.
  friend class TpmNewImplTest;
  explicit TpmNewImpl(tpm_manager::TpmManagerUtility* tpm_manager_utility);
  TpmNewImpl(const TpmNewImpl&) = delete;
  TpmNewImpl& operator=(const TpmNewImpl&) = delete;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_NEW_IMPL_H_

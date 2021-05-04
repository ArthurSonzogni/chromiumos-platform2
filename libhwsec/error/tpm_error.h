// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_TPM_ERROR_H_
#define LIBHWSEC_ERROR_TPM_ERROR_H_

#include <memory>

#include <libhwsec-foundation/error/error.h>

#include "libhwsec/error/tpm_retry_action.h"

namespace hwsec {
namespace error {

// A base class of all kinds of TPM ErrorBase.
class TPMErrorBaseObj : public hwsec_foundation::error::ErrorBaseObj {
 public:
  TPMErrorBaseObj() = default;
  virtual ~TPMErrorBaseObj() = default;

  // Returns what the action should do after this error happen.
  virtual TPMRetryAction ToTPMRetryAction() const = 0;

 protected:
  TPMErrorBaseObj(TPMErrorBaseObj&&) = default;
};
using TPMErrorBase = std::unique_ptr<TPMErrorBaseObj>;

}  // namespace error
}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_TPM_ERROR_H_

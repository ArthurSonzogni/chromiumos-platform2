// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_le_cred_error.h"
#include "cryptohome/error/utilities.h"

namespace cryptohome {

namespace error {

template <typename ErrorType>
bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<ErrorType>& error,
    const ErrorAction action) {
  for (const auto& err : error.const_range()) {
    // NOTE(b/229708597) The underlying StatusChain will prohibit the iteration
    // of the stack soon, and therefore other users of StatusChain should avoid
    // iterating through the StatusChain without consulting the owner of the
    // bug.
    const auto actions = err->local_actions();
    if (actions.count(action) != 0) {
      return true;
    }
  }
  return false;
}

// Instantiate for common types.
template bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<CryptohomeCryptoError>& error,
    const ErrorAction action);
template bool ContainsActionInStack(const hwsec_foundation::status::StatusChain<
                                        error::CryptohomeLECredError>& error,
                                    const ErrorAction action);

}  // namespace error

}  // namespace cryptohome

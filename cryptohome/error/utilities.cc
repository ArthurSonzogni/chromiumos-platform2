// Copyright 2022 The ChromiumOS Authors
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
    PrimaryAction action) {
  for (const auto& err : error.const_range()) {
    // NOTE(b/229708597) The underlying StatusChain will prohibit the iteration
    // of the stack soon, and therefore other users of StatusChain should avoid
    // iterating through the StatusChain without consulting the owner of the
    // bug.
    if (!std::holds_alternative<PrimaryAction>(err->local_actions())) {
      continue;
    }
    if (std::get<PrimaryAction>(err->local_actions()) == action) {
      return true;
    }
  }
  return false;
}

template <typename ErrorType>
bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<ErrorType>& error,
    PossibleAction action) {
  for (const auto& err : error.const_range()) {
    // NOTE(b/229708597) The underlying StatusChain will prohibit the iteration
    // of the stack soon, and therefore other users of StatusChain should avoid
    // iterating through the StatusChain without consulting the owner of the
    // bug.
    if (std::holds_alternative<PrimaryAction>(err->local_actions())) {
      continue;
    }
    if (std::get<PossibleActions>(err->local_actions())[action]) {
      return true;
    }
  }
  return false;
}

// Instantiate for common types.
template bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& error,
    PrimaryAction action);
template bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<CryptohomeCryptoError>& error,
    PrimaryAction action);
template bool ContainsActionInStack(const hwsec_foundation::status::StatusChain<
                                        error::CryptohomeLECredError>& error,
                                    PrimaryAction action);
template bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& error,
    PossibleAction action);
template bool ContainsActionInStack(
    const hwsec_foundation::status::StatusChain<CryptohomeCryptoError>& error,
    PossibleAction action);
template bool ContainsActionInStack(const hwsec_foundation::status::StatusChain<
                                        error::CryptohomeLECredError>& error,
                                    PossibleAction action);

}  // namespace error

}  // namespace cryptohome

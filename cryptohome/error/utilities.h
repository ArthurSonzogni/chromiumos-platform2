// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_UTILITIES_H_
#define CRYPTOHOME_ERROR_UTILITIES_H_

#include <optional>

#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/converter.h"

namespace cryptohome::error {

// Returns true iff any error in the chain contains the given
// action.
template <typename ErrorType>
bool PrimaryActionIs(
    const hwsec_foundation::status::StatusChain<ErrorType>& error,
    const PrimaryAction action) {
  std::optional<PrimaryAction> primary;
  PossibleActions possible;
  ActionsFromStack(error, primary, possible);
  return primary.has_value() && primary.value() == action;
}

template <typename ErrorType>
bool PossibleActionsInclude(
    const hwsec_foundation::status::StatusChain<ErrorType>& error,
    const PossibleAction action) {
  std::optional<PrimaryAction> primary;
  PossibleActions possible;
  ActionsFromStack(error, primary, possible);
  return possible[action];
}

}  // namespace cryptohome::error

#endif  // CRYPTOHOME_ERROR_UTILITIES_H_

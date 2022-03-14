// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_CRYPTOHOME_ERROR_H_
#define CRYPTOHOME_ERROR_CRYPTOHOME_ERROR_H_

#include <libhwsec/error/error.h>

#include <optional>
#include <set>
#include <sstream>
#include <string>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>

#include "cryptohome/error/action.h"

namespace cryptohome {

namespace error {

class CryptohomeError : public hwsec::Error {
 public:
  using MakeStatusTrait = hwsec::DefaultMakeStatus<CryptohomeError>;
  using BaseErrorType = CryptohomeError;

  // Note that while ErrorLocation is represented as an integer, the error
  // location specifier defined in locations.h is its own enum. The reason for
  // this difference is that this integer have to encompasses values greater
  // than the range of the locations specified in locations.h, particularly the
  // codes converted from TPMError and related.
  using ErrorLocation = int64_t;

  // Pull the ErrorAction enum in for convenience.
  using Action = ErrorAction;

  // Standard constructor taking the error location and actions.
  CryptohomeError(
      ErrorLocation loc,
      std::set<Action> actions,
      std::optional<user_data_auth::CryptohomeErrorCode> ec = std::nullopt);
  ~CryptohomeError() override = default;

  // Return the location id in this error.
  ErrorLocation local_location() const { return loc_; }

  // Return the recommended actions in this error (but not the wrapped ones).
  const std::set<Action>& local_actions() const { return actions_; }

  // Return the legacy error code.
  std::optional<user_data_auth::CryptohomeErrorCode> local_legacy_error()
      const {
    return ec_;
  }

  // If we are wrapping a CryptohomeError - concatenate the error id.
  std::string ToString() const override;

 private:
  // From where was the error triggered?
  ErrorLocation loc_;

  // What do we recommend the upper layers do?
  std::set<Action> actions_;

  // The legacy dbus error code.
  std::optional<user_data_auth::CryptohomeErrorCode> ec_;
};

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_CRYPTOHOME_ERROR_H_

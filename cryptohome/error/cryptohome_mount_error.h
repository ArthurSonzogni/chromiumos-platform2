// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_CRYPTOHOME_MOUNT_ERROR_H_
#define CRYPTOHOME_ERROR_CRYPTOHOME_MOUNT_ERROR_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/optional.h>
#include <chromeos/dbus/service_constants.h>

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// This class is a CryptohomeError that holds an extra MountError.
// It is designed for situations that needs the content of the MountError and
// still be compatible with CryptohomeError.
class CryptohomeMountError : public CryptohomeError {
 public:
  using MakeStatusTrait =
      hwsec_foundation::status::DefaultMakeStatus<CryptohomeMountError>;

  // The copyable/movable aspect of this class depends on the base
  // hwsec_foundation::status::Error class. See that class for more info.

  CryptohomeMountError(
      const ErrorLocationPair& loc,
      const std::set<Action>& actions,
      const MountError mount_err,
      const base::Optional<user_data_auth::CryptohomeErrorCode> ec);

  MountError mount_error() const { return mount_error_; }

 private:
  MountError mount_error_;
};

}  // namespace error

// Define an alias in the cryptohome namespace for easier access.
using MountStatus =
    hwsec_foundation::status::StatusChain<error::CryptohomeMountError>;

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_CRYPTOHOME_MOUNT_ERROR_H_

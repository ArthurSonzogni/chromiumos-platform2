// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/cryptohome_mount_error.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/storage/mount_utils.h"

namespace cryptohome {

namespace error {

CryptohomeMountError::CryptohomeMountError(
    const ErrorLocationPair& loc,
    const std::set<Action>& actions,
    const MountError mount_error,
    const std::optional<user_data_auth::CryptohomeErrorCode> ec)
    : CryptohomeError(std::move(loc), actions, ec), mount_error_(mount_error) {}

CryptohomeMountError::CryptohomeMountError(const ErrorLocationPair& loc,
                                           const std::set<Action>& actions,
                                           const MountError mount_error)
    : CryptohomeError(std::move(loc),
                      actions,
                      std::optional<user_data_auth::CryptohomeErrorCode>(
                          MountErrorToCryptohomeError(mount_error))),
      mount_error_(mount_error) {}

}  // namespace error

}  // namespace cryptohome

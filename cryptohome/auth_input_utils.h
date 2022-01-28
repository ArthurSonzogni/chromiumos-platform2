// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_INPUT_UTILS_H_
#define CRYPTOHOME_AUTH_INPUT_UTILS_H_

#include <optional>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/key_objects.h"

namespace cryptohome {

// Converts the AuthInput D-Bus proto into the cryptohome struct.
std::optional<AuthInput> FromProto(
    const user_data_auth::AuthInput& auth_input_proto);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_INPUT_UTILS_H_

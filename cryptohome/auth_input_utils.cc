// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_input_utils.h"

#include <optional>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/key_objects.h"

using brillo::SecureBlob;

namespace cryptohome {

namespace {

AuthInput FromPasswordAuthInput(
    const user_data_auth::PasswordAuthInput& proto) {
  return AuthInput{
      .user_input = SecureBlob(proto.secret()),
  };
}

}  // namespace

std::optional<AuthInput> FromProto(
    const user_data_auth::AuthInput& auth_input_proto) {
  switch (auth_input_proto.input_case()) {
    case user_data_auth::AuthInput::kPasswordInput:
      return FromPasswordAuthInput(auth_input_proto.password_input());
    case user_data_auth::AuthInput::INPUT_NOT_SET:
      LOG(ERROR) << "Empty or unknown auth input";
      return std::nullopt;
  }
  LOG(ERROR) << "Unknown auth input";
  return std::nullopt;
}

}  // namespace cryptohome

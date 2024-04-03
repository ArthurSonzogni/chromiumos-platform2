// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_io/prepare_output.h"

#include <optional>

#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>

#include "cryptohome/key_objects.h"

namespace cryptohome {

user_data_auth::PrepareOutput PrepareOutputToProto(
    const PrepareOutput& prepare_output) {
  user_data_auth::PrepareOutput proto;
  if (prepare_output.cryptohome_recovery_prepare_output) {
    proto.mutable_cryptohome_recovery_output()->set_recovery_request(
        prepare_output.cryptohome_recovery_prepare_output->recovery_rpc_request
            .SerializeAsString());
  }
  return proto;
}

}  // namespace cryptohome

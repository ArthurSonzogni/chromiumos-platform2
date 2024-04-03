// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_IO_PREPARE_OUTPUT_H_
#define CRYPTOHOME_AUTH_IO_PREPARE_OUTPUT_H_

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/key_objects.h"

namespace cryptohome {

// Convert a PrepareOutput struct into a PrepareOutput protobuf.
user_data_auth::PrepareOutput PrepareOutputToProto(
    const PrepareOutput& prepare_output);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_IO_PREPARE_OUTPUT_H_

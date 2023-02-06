// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_H_
#define CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_H_

#include <string>

#include <base/callback.h>
#include <brillo/secure_blob.h>

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

// BiometricsCommandProcessor is a stateless class that processes the biometrics
// commands sent from the biometrics service.
class BiometricsCommandProcessor {
 public:
  // OperationInput is the necessary input for the biometrics auth stack to
  // perform enrollment/authentication. These data can be retrieved by
  // interacting with PinWeaver.
  struct OperationInput {
    brillo::Blob nonce;
    brillo::Blob encrypted_label_seed;
    brillo::Blob iv;
  };

  // OperationOutput contains the data returned from biometrics auth stack after
  // enrollment/authentication for cryptohome to create/authenticate the
  // corresponding AuthFactor.
  struct OperationOutput {
    std::string record_id;
    brillo::SecureBlob auth_secret;
    brillo::SecureBlob auth_pin;
  };

  using OperationCallback =
      base::OnceCallback<void(CryptohomeStatusOr<OperationOutput>)>;

  virtual ~BiometricsCommandProcessor() = default;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_H_

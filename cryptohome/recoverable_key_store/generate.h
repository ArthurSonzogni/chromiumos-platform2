// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_GENERATE_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_GENERATE_H_

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

// Generate the recoverable key store object with the following inputs:
// - |lskf|: The lock screen knowledge factor hash, used to wrap the recovery
// key, allowing the user to recover the recovery key on another device by
// providing the lock screen knowledge factor.
// - |wrong_attempt_label|: The label that identifies the wrong attempt counter
// object at the server side. If the LSKF value didn't change since the last key
// store generation, the same counter label should be reused such that the wrong
// attempt doesn't reset.
// - |keys|: The security domain keys to be wrapped by the recovery key. The
// security domain keys will allow the user to join the security domain on
// another device, which is the goal of generating and uploading recoverable key
// stores.
// - |cert|: The certificate which contains the server backend public key used
// for wrapping the recovery key, such that decryption attempts using the LSKF
// can only happen in the server.
CryptohomeStatusOr<RecoverableKeyStore> GenerateRecoverableKeyStore(
    const LockScreenKnowledgeFactor& lskf,
    const brillo::Blob& wrong_attempt_label,
    const SecurityDomainKeys& keys,
    const RecoverableKeyStoreBackendCert& cert);

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_GENERATE_H_

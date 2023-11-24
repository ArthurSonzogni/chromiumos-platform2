// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_TYPE_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_TYPE_H_

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_box.h>

namespace cryptohome {

// Key objects associated with a security domain. It includes an asymmetric
// security domain member key pair and a wrapping key. They will be uploaded to
// the corresponding recoverable key store and protected by:
// - The private key of the key pair will be wrapped by the wrapping key
// - The wrapping key will be wrapped by the recovery key
// - The recovery key will be protected by the lock screen knowledge factor
struct SecurityDomainKeys {
  hwsec_foundation::secure_box::KeyPair key_pair;
  brillo::SecureBlob wrapping_key;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_TYPE_H_

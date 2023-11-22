// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_TYPE_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_TYPE_H_

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
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

// The lock screen knowledge factor, along with all associated data necessary
// for generating the recoverable key store.
struct LockScreenKnowledgeFactor {
  // Type of the lock screen knowledge factor.
  LockScreenKnowledgeFactorType lskf_type;
  // The lock screen knowledge factor hash algorithm.
  LockScreenKnowledgeFactorHashAlgorithm algorithm;
  // The salt used for hashing the lock screen knowledge factor.
  brillo::Blob salt;
  // The hash result of the lock screen knowledge factor using |salt| as salt.
  brillo::SecureBlob hash;
};

// An object that includes a backend certificate and its associated data.
struct RecoverableKeyStoreBackendCert {
  uint64_t version;
  // In SecureBox-encoded format.
  brillo::Blob public_key;
  // TODO(b/312628857): Add |path| in RecoverableKeyStoreBackendCert as
  // well because it needs to be set in the RecoverableKeyStore proto.
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_TYPE_H_

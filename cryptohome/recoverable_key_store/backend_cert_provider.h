// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_H_

#include <optional>

#include <brillo/secure_blob.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

// Provider for the recoverable key store service backend certificates. It can
// be used for obtain the certificate necessary for recoverable key store
// generation, or check whether the current key store is out of date and needs
// regeneration.
class RecoverableKeyStoreBackendCertProvider {
 public:
  RecoverableKeyStoreBackendCertProvider() = default;
  RecoverableKeyStoreBackendCertProvider(
      const RecoverableKeyStoreBackendCertProvider&) = delete;
  RecoverableKeyStoreBackendCertProvider& operator=(
      const RecoverableKeyStoreBackendCertProvider&) = delete;
  virtual ~RecoverableKeyStoreBackendCertProvider() = default;

  // Get a recoverable key store service backend certificate. This should try to
  // return a certificate in the most up-to-date published certificate list in
  // the server side. This doesn't guarantee returning the same cert or version
  // among multiple calls because there will be multiple available certificates
  // at the same time, and also the certificate list might be updated. A
  // recoverable key store should be regenerated if it contains an older list
  // version than the current one.
  virtual std::optional<RecoverableKeyStoreBackendCert> GetBackendCert()
      const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_H_

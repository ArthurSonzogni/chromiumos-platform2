// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_IMPL_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_IMPL_H_

#include "cryptohome/recoverable_key_store/backend_cert_provider.h"

#include <optional>

#include <brillo/secure_blob.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

class RecoverableKeyStoreBackendCertProviderImpl
    : public RecoverableKeyStoreBackendCertProvider {
 public:
  RecoverableKeyStoreBackendCertProviderImpl() = default;

  RecoverableKeyStoreBackendCertProviderImpl(
      const RecoverableKeyStoreBackendCertProviderImpl&) = delete;
  RecoverableKeyStoreBackendCertProviderImpl& operator=(
      const RecoverableKeyStoreBackendCertProviderImpl&) = delete;

  // A random certificate from the current available list will be returned, for
  // load-balancing.
  std::optional<RecoverableKeyStoreBackendCert> GetBackendCert() const override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_IMPL_H_

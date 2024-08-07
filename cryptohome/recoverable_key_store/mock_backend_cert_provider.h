// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_MOCK_BACKEND_CERT_PROVIDER_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_MOCK_BACKEND_CERT_PROVIDER_H_

#include <optional>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

class MockRecoverableKeyStoreBackendCertProvider
    : public RecoverableKeyStoreBackendCertProvider {
 public:
  MockRecoverableKeyStoreBackendCertProvider() = default;

  MOCK_METHOD(std::optional<RecoverableKeyStoreBackendCert>,
              GetBackendCert,
              (),
              (const, override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_MOCK_BACKEND_CERT_PROVIDER_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_provider_impl.h"

#include <optional>

#include <brillo/secure_blob.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

std::optional<RecoverableKeyStoreBackendCert>
RecoverableKeyStoreBackendCertProviderImpl::GetBackendCert() const {
  return std::nullopt;
}

}  // namespace cryptohome

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_PINWEAVER_MANAGER_H_
#define LIBHWSEC_FUZZED_PINWEAVER_MANAGER_H_

#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"
#include "libhwsec/fuzzed/basic_objects.h"

namespace hwsec {

template <>
struct FuzzedObject<LECredentialManager::CheckCredentialReply> {
  LECredentialManager::CheckCredentialReply operator()(
      FuzzedDataProvider& provider) const {
    return LECredentialManager::CheckCredentialReply{
        .he_secret = FuzzedObject<brillo::SecureBlob>()(provider),
        .reset_secret = FuzzedObject<brillo::SecureBlob>()(provider),
    };
  }
};

template <>
struct FuzzedObject<LECredentialManager::StartBiometricsAuthReply> {
  LECredentialManager::StartBiometricsAuthReply operator()(
      FuzzedDataProvider& provider) const {
    return LECredentialManager::StartBiometricsAuthReply{
        .server_nonce = FuzzedObject<brillo::Blob>()(provider),
        .iv = FuzzedObject<brillo::Blob>()(provider),
        .encrypted_he_secret = FuzzedObject<brillo::Blob>()(provider),
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_PINWEAVER_MANAGER_H_

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_KEY_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_MOCK_KEY_MANAGEMENT_H_

#include <cstdint>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/key_management.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class BackendTpm2;

class MockKeyManagement : public KeyManagement {
 public:
  MOCK_METHOD(StatusOr<absl::flat_hash_set<KeyAlgoType>>,
              GetSupportedAlgo,
              (),
              (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              CreateKey,
              (const OperationPolicySetting& policy,
               KeyAlgoType key_algo,
               CreateKeyOptions options),
              (override));
  MOCK_METHOD(StatusOr<ScopedKey>,
              LoadKey,
              (const OperationPolicy& policy, const brillo::Blob& key_blob),
              (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              CreateAutoReloadKey,
              (const OperationPolicySetting& policy,
               KeyAlgoType key_algo,
               CreateKeyOptions options),
              (override));
  MOCK_METHOD(StatusOr<ScopedKey>,
              LoadAutoReloadKey,
              (const OperationPolicy& policy, const brillo::Blob& key_blob),
              (override));
  MOCK_METHOD(StatusOr<ScopedKey>,
              GetPersistentKey,
              (PersistentKeyType key_type),
              (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetPubkeyHash, (Key key), (override));
  MOCK_METHOD(Status, Flush, (Key key), (override));
  MOCK_METHOD(Status, ReloadIfPossible, (Key key), (override));
  MOCK_METHOD(StatusOr<ScopedKey>,
              SideLoadKey,
              (uint32_t key_handle),
              (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetKeyHandle, (Key key), (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_KEY_MANAGEMENT_H_

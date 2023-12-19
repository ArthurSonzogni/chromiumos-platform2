// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_RECOVERABLE_KEY_STORE_H_
#define CRYPTOHOME_AUTH_BLOCKS_RECOVERABLE_KEY_STORE_H_

#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>

#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider.h"

namespace cryptohome {

// Create the RecoverableKeyStoreState using the given parameters. This is the
// common implementation that can be used by AuthBlocks that need to generate
// recoverable key stores.
CryptohomeStatusOr<RecoverableKeyStoreState> CreateRecoverableKeyStoreState(
    KnowledgeFactorType knowledge_factor_type,
    const AuthInput& auth_input,
    const AuthFactorMetadata& metadata,
    const RecoverableKeyStoreBackendCertProvider& cert_provider);

// If the content of |state| is up to date, return a nullopt.
// Otherwise, regenerate the updated state based on |state|. If success, return
// the regenerated state. Otherwise, return an error.
CryptohomeStatusOr<std::optional<RecoverableKeyStoreState>>
MaybeUpdateRecoverableKeyStoreState(
    const RecoverableKeyStoreState& state,
    KnowledgeFactorType knowledge_factor_type,
    const AuthInput& auth_input,
    const AuthFactorMetadata& metadata,
    const RecoverableKeyStoreBackendCertProvider& cert_provider);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_RECOVERABLE_KEY_STORE_H_

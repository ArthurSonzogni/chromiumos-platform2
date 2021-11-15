// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/sync_to_async_auth_block_adapter.h"

#include <memory>
#include <utility>

namespace cryptohome {

SyncToAsyncAuthBlockAdapter::SyncToAsyncAuthBlockAdapter(
    std::unique_ptr<SyncAuthBlock> delegate)
    : AuthBlock(delegate->derivation_type()), delegate_(std::move(delegate)) {
  DCHECK(delegate_);
}

void SyncToAsyncAuthBlockAdapter::Create(const AuthInput& user_input,
                                         CreateCallback callback) {
  auto state = std::make_unique<AuthBlockState>();
  auto key_blobs = std::make_unique<KeyBlobs>();
  CryptoError error =
      delegate_->Create(user_input, state.get(), key_blobs.get());
  if (error != CryptoError::CE_NONE) {
    std::move(callback).Run(error, nullptr, nullptr);
    return;
  }
  std::move(callback).Run(CryptoError::CE_NONE, std::move(key_blobs),
                          std::move(state));
}

void SyncToAsyncAuthBlockAdapter::Derive(const AuthInput& user_input,
                                         const AuthBlockState& state,
                                         DeriveCallback callback) {
  auto key_blobs = std::make_unique<KeyBlobs>();
  CryptoError error = delegate_->Derive(user_input, state, key_blobs.get());
  if (error != CryptoError::CE_NONE) {
    std::move(callback).Run(error, nullptr);
    return;
  }
  std::move(callback).Run(CryptoError::CE_NONE, std::move(key_blobs));
}

}  // namespace cryptohome

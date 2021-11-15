// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_SYNC_TO_ASYNC_AUTH_BLOCK_ADAPTER_H_
#define CRYPTOHOME_AUTH_BLOCKS_SYNC_TO_ASYNC_AUTH_BLOCK_ADAPTER_H_

#include <memory>

#include <base/callback.h>

#include "cryptohome/auth_blocks/auth_block.h"

namespace cryptohome {

// This is an adapter to adapt synchronous auth block to asynchronous auth block
// interface.
class SyncToAsyncAuthBlockAdapter : public AuthBlock {
 public:
  explicit SyncToAsyncAuthBlockAdapter(std::unique_ptr<SyncAuthBlock> delegate);

  ~SyncToAsyncAuthBlockAdapter() = default;

  // Calls the synchronous AuthBlock::Create() on delegate_.
  void Create(const AuthInput& user_input, CreateCallback callback) override;

  // Calls the synchronous AuthBlock::Derive() on delegate_. AuthBlockState
  // must be the one returned from Create().
  void Derive(const AuthInput& user_input,
              const AuthBlockState& state,
              DeriveCallback callback) override;

 private:
  // The synchronous auth block to be called.
  std::unique_ptr<SyncAuthBlock> delegate_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_SYNC_TO_ASYNC_AUTH_BLOCK_ADAPTER_H_

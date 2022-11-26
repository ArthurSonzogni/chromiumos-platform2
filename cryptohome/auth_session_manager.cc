// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/notreached.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/status.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session_map.h"

using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;

namespace cryptohome {

AuthSessionManager::AuthSessionManager(
    Crypto* crypto,
    Platform* platform,
    UserSessionMap* user_session_map,
    KeysetManagement* keyset_management,
    AuthBlockUtility* auth_block_utility,
    AuthFactorManager* auth_factor_manager,
    UserSecretStashStorage* user_secret_stash_storage)
    : crypto_(crypto),
      platform_(platform),
      user_session_map_(user_session_map),
      keyset_management_(keyset_management),
      auth_block_utility_(auth_block_utility),
      auth_factor_manager_(auth_factor_manager),
      user_secret_stash_storage_(user_secret_stash_storage) {
  // Preconditions
  DCHECK(crypto_);
  DCHECK(platform_);
  DCHECK(user_session_map_);
  DCHECK(keyset_management_);
  DCHECK(auth_block_utility_);
  DCHECK(auth_factor_manager_);
  DCHECK(user_secret_stash_storage_);
}

CryptohomeStatusOr<AuthSession*> AuthSessionManager::CreateAuthSession(
    const std::string& account_id, uint32_t flags, AuthIntent auth_intent) {
  // The lifetime of AuthSessionManager instance will outlast AuthSession
  // which is why usage of |Unretained| is safe.
  auto on_timeout = base::BindOnce(&AuthSessionManager::ExpireAuthSession,
                                   base::Unretained(this));
  // Assumption here is that keyset_management_ will outlive this AuthSession.
  CryptohomeStatusOr<std::unique_ptr<AuthSession>> auth_session =
      AuthSession::Create(account_id, flags, auth_intent, std::move(on_timeout),
                          crypto_, platform_, user_session_map_,
                          keyset_management_, auth_block_utility_,
                          auth_factor_manager_, user_secret_stash_storage_);

  if (!auth_session.ok()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionManagerCreateFailed),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_UNUSABLE_VAULT);
  }

  auto token = auth_session.value()->token();
  if (auth_sessions_.count(token) > 0) {
    LOG(ERROR) << "AuthSession token collision";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionManagerTokenCollision),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_UNUSABLE_VAULT);
  }

  auth_sessions_.emplace(token, std::move(auth_session.value()));
  return auth_sessions_[token].get();
}

bool AuthSessionManager::RemoveAuthSession(
    const base::UnguessableToken& token) {
  const auto iter = auth_sessions_.find(token);
  if (iter == auth_sessions_.end())
    return false;
  auth_sessions_.erase(iter);
  return true;
}

bool AuthSessionManager::RemoveAuthSession(
    const std::string& serialized_token) {
  std::optional<base::UnguessableToken> token =
      AuthSession::GetTokenFromSerializedString(serialized_token);
  if (!token.has_value()) {
    LOG(ERROR) << "Unparsable AuthSession token for removal";
    return false;
  }
  return RemoveAuthSession(token.value());
}

void AuthSessionManager::ExpireAuthSession(
    const base::UnguessableToken& token) {
  if (!RemoveAuthSession(token)) {
    // All active auth sessions should be tracked by the manager, so report it
    // if the just-expired session is unknown.
    NOTREACHED() << "Failed to remove expired AuthSession.";
  }
}

AuthSession* AuthSessionManager::FindAuthSession(
    const std::string& serialized_token) const {
  std::optional<base::UnguessableToken> token =
      AuthSession::GetTokenFromSerializedString(serialized_token);
  if (!token.has_value()) {
    LOG(ERROR) << "Unparsable AuthSession token for find";
    return nullptr;
  }
  return FindAuthSession(token.value());
}

AuthSession* AuthSessionManager::FindAuthSession(
    const base::UnguessableToken& token) const {
  auto it = auth_sessions_.find(token);
  if (it == auth_sessions_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace cryptohome

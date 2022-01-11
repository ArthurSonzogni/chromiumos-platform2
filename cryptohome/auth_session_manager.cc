// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/bind.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/keyset_management.h"

namespace cryptohome {

AuthSessionManager::AuthSessionManager(KeysetManagement* keyset_management)
    : keyset_management_(keyset_management) {}

AuthSession* AuthSessionManager::CreateAuthSession(
    const std::string& account_id, uint32_t flags) {
  // The lifetime of AuthSessionManager instance will outlast AuthSession
  // which is why usage of |Unretained| is safe.
  auto on_timeout = base::BindOnce(&AuthSessionManager::ExpireAuthSession,
                                   base::Unretained(this));
  // Assumption here is that keyset_management_ will outlive this AuthSession.
  std::unique_ptr<AuthSession> auth_session = std::make_unique<AuthSession>(
      account_id, flags, std::move(on_timeout), keyset_management_);

  auto token = auth_session->token();
  if (auth_sessions_.count(token) > 0) {
    LOG(ERROR) << "AuthSession token collision";
    return nullptr;
  }

  auth_sessions_.emplace(token, std::move(auth_session));
  return auth_sessions_[token].get();
}

void AuthSessionManager::RemoveAuthSession(
    const base::UnguessableToken& token) {
  auth_sessions_.erase(token);
}

void AuthSessionManager::RemoveAuthSession(
    const std::string& serialized_token) {
  std::optional<base::UnguessableToken> token =
      AuthSession::GetTokenFromSerializedString(serialized_token);
  if (!token.has_value()) {
    LOG(ERROR) << "Unparsable AuthSession token for removal";
    return;
  }
  RemoveAuthSession(token.value());
}

void AuthSessionManager::ExpireAuthSession(
    const base::UnguessableToken& token) {
  RemoveAuthSession(token);
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

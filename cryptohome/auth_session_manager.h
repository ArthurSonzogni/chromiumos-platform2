// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_MANAGER_H_
#define CRYPTOHOME_AUTH_SESSION_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include <base/unguessable_token.h>

#include "cryptohome/auth_session.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/UserDataAuth.pb.h"

namespace cryptohome {

class AuthSessionManager {
 public:
  explicit AuthSessionManager(KeysetManagement* keyset_management);
  ~AuthSessionManager() = default;
  AuthSessionManager(AuthSessionManager&) = delete;
  AuthSessionManager& operator=(AuthSessionManager&) = delete;

  // Creates new auth session for account_id. AuthSessionManager owns the
  // created AuthSession and the method returns a pointer to it.
  AuthSession* CreateAuthSession(const std::string& account_id, uint32_t flags);

  // Removes existing auth session with token.
  void RemoveAuthSession(const base::UnguessableToken& token);

  // Overload for remove to avoid deserialization client side.
  void RemoveAuthSession(const std::string& serialized_token);

  // Finds existing auth session with token.
  AuthSession* FindAuthSession(const base::UnguessableToken& token) const;

  // Overload for find to avoid deserialization client side.
  AuthSession* FindAuthSession(const std::string& serialized_token) const;

 private:
  KeysetManagement* keyset_management_;

  // Callback for session timeout. Currently just disambiguates
  // RemoveAuthSession overload for the callback.
  void ExpireAuthSession(const base::UnguessableToken& token);

  // Defines a type for tracking Auth Sessions by token.
  typedef std::map<const base::UnguessableToken, std::unique_ptr<AuthSession>>
      AuthSessionMap;

  AuthSessionMap auth_sessions_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_MANAGER_H_

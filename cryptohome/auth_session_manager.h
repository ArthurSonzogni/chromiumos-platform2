// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_MANAGER_H_
#define CRYPTOHOME_AUTH_SESSION_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include <base/unguessable_token.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session_map.h"
#include "featured/feature_library.h"

namespace cryptohome {

class InUseAuthSession;

class AuthSessionManager {
 public:
  // The passed raw pointers are unowned and must outlive the created object.
  explicit AuthSessionManager(
      Crypto* crypto,
      Platform* platform,
      UserSessionMap* user_session_map,
      KeysetManagement* keyset_management,
      AuthBlockUtility* auth_block_utility,
      AuthFactorManager* auth_factor_manager,
      UserSecretStashStorage* user_secret_stash_storage);
  ~AuthSessionManager() = default;
  AuthSessionManager(AuthSessionManager&) = delete;
  AuthSessionManager& operator=(AuthSessionManager&) = delete;

  // Creates new auth session for account_id. AuthSessionManager owns the
  // created AuthSession and the method returns a pointer to it.
  CryptohomeStatusOr<InUseAuthSession> CreateAuthSession(
      const std::string& account_id, uint32_t flags, AuthIntent auth_intent);

  // Removes existing auth session with token. Returns false if there's no auth
  // session with this token.
  bool RemoveAuthSession(const base::UnguessableToken& token);

  // Overload for remove to avoid deserialization client side. Returns false if
  // there's no auth session with the given token.
  bool RemoveAuthSession(const std::string& serialized_token);

  // Finds existing auth session with token.
  InUseAuthSession FindAuthSession(const base::UnguessableToken& token);

  // Overload for find to avoid deserialization client side.
  InUseAuthSession FindAuthSession(const std::string& serialized_token);

  void set_feature_lib(feature::PlatformFeaturesInterface* feature_lib) {
    feature_lib_ = feature_lib;
  }

 private:
  friend class InUseAuthSession;

  // Unowned; must outlive `this`.
  Crypto* const crypto_;
  // Unowned; must outlive `this`.
  Platform* const platform_;
  // Unowned; must outlive `this`.
  UserSessionMap* const user_session_map_;
  // Unowned; must outlive `this`.
  KeysetManagement* const keyset_management_;
  // Unowned; must outlive `this`.
  AuthBlockUtility* const auth_block_utility_;
  // Unowned; must outlive `this`.
  AuthFactorManager* const auth_factor_manager_;
  // Unowned; must outlive `this`.
  UserSecretStashStorage* const user_secret_stash_storage_;
  // // Unowned; must outlive `this`.
  // This holds the object that checks for feature enabled.
  feature::PlatformFeaturesInterface* feature_lib_;

  // Callback for session timeout. Currently just disambiguates
  // RemoveAuthSession overload for the callback.
  void ExpireAuthSession(const base::UnguessableToken& token);

  // Run as the destructor for InUseAuthSession, signaling that any active dbus
  // calls that referenced the AuthSession have now finished.
  void MarkNotInUse(std::unique_ptr<AuthSession> session);

  // Defines a type for tracking Auth Sessions by token.
  // For AuthSessions in active use, the unique_ptr for the AuthSession for a
  // given token will be nullptr, as the ownership is being held by an
  // InUseAuthSession object.
  using AuthSessionMap =
      std::map<const base::UnguessableToken, std::unique_ptr<AuthSession>>;

  AuthSessionMap auth_sessions_;
};

// AuthSessionManager constructs an InUseAuthSession from an underlying
// AuthSession, and returns that InUseAuthSession class. Anytime the
// InUseAuthSession class is alive in any scope it indicates the underlying
// Authsession has an ongoing dbus operation with that token, and thus cannot be
// used/constructed again until it is returned. The InUseAuthSession object
// destructor returns the actual AuthSession object back to AuthSessionManager,
// indicating that AuthSession is again free for use.
class InUseAuthSession {
 public:
  InUseAuthSession();
  InUseAuthSession(InUseAuthSession&& auth_session);
  InUseAuthSession& operator=(InUseAuthSession&& auth_session);

  ~InUseAuthSession();

  AuthSession& operator*() { return *session_; }
  AuthSession* operator->() { return session_.get(); }

  AuthSession* Get();
  CryptohomeStatus AuthSessionStatus();

 private:
  friend class AuthSessionManager;

  InUseAuthSession(AuthSessionManager& manager,
                   bool is_token_valid,
                   std::unique_ptr<AuthSession> session);

  AuthSessionManager* manager_;
  bool is_session_active_;
  std::unique_ptr<AuthSession> session_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_MANAGER_H_

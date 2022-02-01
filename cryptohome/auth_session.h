// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_H_
#define CRYPTOHOME_AUTH_SESSION_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/timer/timer.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash_storage.h"

namespace cryptohome {

// This enum holds the states an AuthSession could be in during the session.
enum class AuthStatus {
  // kAuthStatusFurtherFactorRequired is a state where the session is waiting
  // for one or more factors so that the session can continue the processes of
  // authenticating a user. This is the state the AuthSession starts in by
  // default.
  kAuthStatusFurtherFactorRequired,
  // kAuthStatusTimedOut tells the user to restart the AuthSession because
  // the session has timed out.
  kAuthStatusTimedOut,
  // kAuthStatusAuthenticated tells the user that the session is authenticated
  // and that file system keys are available should they be required.
  kAuthStatusAuthenticated
  // TODO(crbug.com/1154912): Complete the implementation of AuthStatus.
};

// This class starts a session for the user to authenticate with their
// credentials.
class AuthSession final {
 public:
  // Caller needs to ensure that the KeysetManagement*, AuthFactorManager* and
  // UserSecretStashStorage* outlive the instance of AuthSession.
  AuthSession(
      std::string username,
      unsigned int flags,
      base::OnceCallback<void(const base::UnguessableToken&)> on_timeout,
      KeysetManagement* keyset_management,
      AuthFactorManager* auth_factor_manager,
      UserSecretStashStorage* user_secret_stash_storage);
  ~AuthSession() = default;

  // Returns the full unhashed user name.
  std::string username() const { return username_; }

  // Returns the token which is used to identify the current AuthSession.
  const base::UnguessableToken& token() const { return token_; }
  const std::string& serialized_token() const { return serialized_token_; }

  // This function return the current status of this AuthSession.
  AuthStatus GetStatus() const { return status_; }

  // AddCredentials is called when newly created or existing user wants to add
  // new credentials.
  user_data_auth::CryptohomeErrorCode AddCredentials(
      const user_data_auth::AddCredentialsRequest& request);

  // UpdateCredential is called when an existing user wants to update
  // an existing credential.
  user_data_auth::CryptohomeErrorCode UpdateCredential(
      const user_data_auth::UpdateCredentialRequest& request);

  // AddCredentials is called when newly created or existing user wants to add
  // new credentials.
  user_data_auth::CryptohomeErrorCode AddAuthFactor(
      const user_data_auth::AddAuthFactorRequest& request);

  // Authenticate is called when the user wants to authenticate the current
  // AuthSession. It may be called multiple times depending on errors or various
  // steps involved in multi-factor authentication.
  user_data_auth::CryptohomeErrorCode Authenticate(
      const cryptohome::AuthorizationRequest& authorization_request);

  // Return a const reference to FileSystemKeyset.
  const FileSystemKeyset file_system_keyset() const {
    return FileSystemKeyset(*vault_keyset_);
  }

  // Transfer ownership of password verifier that can be used to verify
  // credentials during unlock.
  std::unique_ptr<CredentialVerifier> TakeCredentialVerifier();

  // This function returns if the user existed when the auth session started.
  bool user_exists() const { return user_exists_; }

  // This function returns if the user has any credential configured. When a
  // credential is added, this value changes from false to true.
  bool user_has_configured_credential() const {
    return user_has_configured_credential_;
  }

  // This function returns if the AuthSession is being setup for an ephemeral
  // user.
  bool ephemeral_user() const { return is_ephemeral_user_; }

  // Returns the key data with which this AuthSession is authenticated with.
  cryptohome::KeyData current_key_data() const { return key_data_; }

  // Returns the map of Key label and KeyData that will be used as a result of
  // StartAuthSession request.
  const std::map<std::string, cryptohome::KeyData>& key_label_data() const {
    return key_label_data_;
  }

  // Static function which returns a serialized token in a vector format. The
  // token is serialized into two uint64_t values which are stored in string of
  // size 16 bytes. The first 8 bytes represent the high value of the serialized
  // token, the next 8 represent the low value of the serialized token.
  static std::optional<std::string> GetSerializedStringFromToken(
      const base::UnguessableToken& token);

  // Static function which returns UnguessableToken object after deconstructing
  // the string formed in GetSerializedStringFromToken.
  static std::optional<base::UnguessableToken> GetTokenFromSerializedString(
      const std::string& serialized_token);

  // Extends the timer for the AuthSession by kAuthSessionExtensionInMinutes.
  user_data_auth::CryptohomeErrorCode ExtendTimer(
      const base::TimeDelta kAuthSessionExtension);

  // Set status for testing only.
  void SetStatus(const AuthStatus status) { status_ = status; }

 private:
  AuthSession() = delete;
  // AuthSessionTimedOut is called when the session times out and cleans up
  // credentials that may be in memory. |on_timeout_| is also called to remove
  // this |AuthSession| reference from |UserDataAuth|.
  void AuthSessionTimedOut();

  // This function returns credentials based on the state of the current
  // |AuthSession|.
  std::unique_ptr<Credentials> GetCredentials(
      const cryptohome::AuthorizationRequest& authorization_request,
      MountError* error);

  const std::string username_;
  const base::UnguessableToken token_;
  const std::string serialized_token_;

  // AuthSession's flag configuration.
  const bool is_ephemeral_user_;

  AuthStatus status_ = AuthStatus::kAuthStatusFurtherFactorRequired;
  base::OneShotTimer timer_;
  base::TimeTicks start_time_;
  base::OnceCallback<void(const base::UnguessableToken&)> on_timeout_;

  std::unique_ptr<AuthFactor> auth_factor_;
  // The creator of the AuthSession object is responsible for the life of
  // KeysetManagement object.
  // TODO(crbug.com/1171024): Change KeysetManagement to use AuthBlock.
  KeysetManagement* const keyset_management_;
  // Unowned pointer.
  AuthFactorManager* const auth_factor_manager_;
  // Unowned pointer.
  UserSecretStashStorage* const user_secret_stash_storage_;
  // This is used by User Session to verify users credentials at unlock.
  std::unique_ptr<CredentialVerifier> credential_verifier_;
  // Used to decrypt/ encrypt & store credentials.
  std::unique_ptr<VaultKeyset> vault_keyset_;
  // Used to store key meta data.
  cryptohome::KeyData key_data_;
  // Whether the user existed at the time this object was constructed.
  bool user_exists_ = false;
  // Whether the user has any credential configured so far.
  bool user_has_configured_credential_ = false;
  // Map to store the label and public KeyData.
  // TODO(crbug.com/1171024): Change this to AuthFactor
  std::map<std::string, cryptohome::KeyData> key_label_data_;

  friend class AuthSessionTest;
  FRIEND_TEST(AuthSessionTest, AddCredentialNewUser);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewUserTwice);
  FRIEND_TEST(AuthSessionTest, AddCredentialNewEphemeralUser);
  FRIEND_TEST(AuthSessionTest, AuthenticateExistingUser);
  FRIEND_TEST(AuthSessionTest, TimeoutTest);
  FRIEND_TEST(AuthSessionTest, GetCredentialRegularUser);
  FRIEND_TEST(AuthSessionTest, GetCredentialKioskUser);
  FRIEND_TEST(UserDataAuthExTest, MountUnauthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
  FRIEND_TEST(UserDataAuthExTest, ExtendAuthSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_

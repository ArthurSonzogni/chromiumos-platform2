// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_H_
#define CRYPTOHOME_AUTH_SESSION_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/timer/timer.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/UserDataAuth.pb.h"

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
  // Caller needs to ensure that the the KeysetManagement* outlives the instance
  // of AuthSession.
  AuthSession(
      std::string username,
      unsigned int flags,
      base::OnceCallback<void(const base::UnguessableToken&)> on_timeout,
      KeysetManagement* keyset_management);
  ~AuthSession();

  // Returns the full unhashed user name.
  std::string username() const { return username_; }

  // Returns the token which is used to identify the current AuthSession.
  const base::UnguessableToken& token() { return token_; }

  // This function return the current status of this AuthSession.
  AuthStatus GetStatus() const { return status_; }

  // This function return if the user that AuthSession was created with exists.
  bool user_exists() const { return user_exists_; }

  // AddCredentials is called when newly created or existing user wants to add
  // new credentials.
  // TODO(crbug.com/1181102): Add functionality for adding credentials for an
  // existing user.
  user_data_auth::CryptohomeErrorCode AddCredentials(
      const user_data_auth::AddCredentialsRequest& request);

  // Authenticate is called when the user wants to authenticate the current
  // AuthSession. It may be called multiple times depending on errors or various
  // steps involved in multi-factor authentication.
  user_data_auth::CryptohomeErrorCode Authenticate(
      const cryptohome::AuthorizationRequest& authorization_request);

  // Return a const reference to FileSystemKeyset.
  const FileSystemKeyset file_system_keyset() {
    return auth_factor_->GetFileSystemKeyset();
  }

  // Transfer ownership of password verifier that can be used to verify
  // credentials during unlock.
  std::unique_ptr<CredentialVerifier> TakeCredentialVerifier();

  // This function returns the current index of the keyset that was used to
  // Authenticate. This is useful during verification of challenge credentials.
  int key_index() { return auth_factor_->GetKeyIndex(); }

  // This functions returns if user existed when the AuthSession was started.
  bool user_exists() { return user_exists_; }

  // Returns the key data with which this AuthSession is authenticated with.
  cryptohome::KeyData current_key_data() { return auth_factor_->GetKeyData(); }

  // Returns the map of Key label and KeyData that will be used as a result of
  // StartAuthSession request.
  const std::map<std::string, cryptohome::KeyData>& key_label_data() {
    return key_label_data_;
  }

  // Static function which returns a serialized token in a vector format. The
  // token is serialized into two uint64_t values which are stored in string of
  // size 16 bytes. The first 8 bytes represent the high value of the serialized
  // token, the next 8 represent the low value of the serialized token.
  static base::Optional<std::string> GetSerializedStringFromToken(
      const base::UnguessableToken& token);

  // Static function which returns UnguessableToken object after deconstructing
  // the string formed in GetSerializedStringFromToken.
  static base::Optional<base::UnguessableToken> GetTokenFromSerializedString(
      const std::string& serialized_token);

 private:
  AuthSession() = delete;
  // AuthSessionTimedOut is called when the session times out and cleans up
  // credentials that may be in memory. |on_timeout_| is also called to remove
  // this |AuthSession| reference from |UserDataAuth|.
  void AuthSessionTimedOut();

  // Process the parameters received when constructing an |AuthSession|.
  void ProcessFlags(unsigned int flags);

  // This function returns credentials based on the state of the current
  // |AuthSession|.
  std::unique_ptr<Credentials> GetCredentials(
      const cryptohome::AuthorizationRequest& authorization_request,
      MountError* error);

  std::string username_;
  bool is_kiosk_user_;
  base::UnguessableToken token_;

  AuthStatus status_ = AuthStatus::kAuthStatusFurtherFactorRequired;
  base::OneShotTimer timer_;
  base::OnceCallback<void(const base::UnguessableToken&)> on_timeout_;

  std::unique_ptr<AuthFactor> auth_factor_;
  // The creator of the AuthSession object is responsible for the life of
  // KeysetManagement object.
  // TODO(crbug.com/1171024): Change KeysetManagement to use AuthBlock.
  KeysetManagement* keyset_management_;
  // Bool that determines some state with AuthSession especially with adding
  // credentials.
  bool user_exists_;
  // Map to store the label and public KeyData.
  // TODO(crbug.com/1171024): Change this to AuthFactor
  std::map<std::string, cryptohome::KeyData> key_label_data_;
  FRIEND_TEST(AuthSessionTest, TimeoutTest);
  FRIEND_TEST(AuthSessionTest, GetCredentialRegularUser);
  FRIEND_TEST(AuthSessionTest, GetCredentialKioskUser);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_

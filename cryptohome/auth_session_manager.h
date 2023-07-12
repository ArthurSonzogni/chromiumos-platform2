// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_MANAGER_H_
#define CRYPTOHOME_AUTH_SESSION_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include <base/time/clock.h>
#include <base/time/tick_clock.h>
#include <base/time/time.h>
#include <base/timer/wall_clock_timer.h>
#include <base/unguessable_token.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_session.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/platform.h"
#include "cryptohome/username.h"

namespace cryptohome {

class InUseAuthSession;

class AuthSessionManager {
 public:
  // The default timeout duration for sessions.
  static constexpr base::TimeDelta kAuthTimeout = base::Minutes(5);

  // Construct a session manager that will use the given backing APIs to create
  // new AuthSession objects.
  explicit AuthSessionManager(AuthSession::BackingApis backing_apis);

  AuthSessionManager(AuthSessionManager&) = delete;
  AuthSessionManager& operator=(AuthSessionManager&) = delete;

  ~AuthSessionManager() = default;

  // Creates new auth session for account_id. AuthSessionManager owns the
  // created AuthSession and the method returns a pointer to it.
  CryptohomeStatusOr<InUseAuthSession> CreateAuthSession(
      const Username& account_id, uint32_t flags, AuthIntent auth_intent);

  // Adds a pre-existing auth session to the manager, which will take ownership
  // over the session.
  InUseAuthSession AddAuthSession(std::unique_ptr<AuthSession> auth_session);

  // Removes existing auth session with token. Returns false if there's no auth
  // session with this token.
  bool RemoveAuthSession(const base::UnguessableToken& token);

  // Overload for remove to avoid deserialization client side. Returns false if
  // there's no auth session with the given token.
  bool RemoveAuthSession(const std::string& serialized_token);

  // Removes all the authsession and calls their destructor. This is supposed to
  // be used when UnMountall() API is called.
  void RemoveAllAuthSessions();

  // Finds existing auth session with token.
  InUseAuthSession FindAuthSession(const base::UnguessableToken& token);

  // Overload for find to avoid deserialization client side.
  InUseAuthSession FindAuthSession(const std::string& serialized_token);

  // Used to set the auth factor status update callback inside class so it could
  // be passed to each auth session.
  void SetAuthFactorStatusUpdateCallback(
      const AuthFactorStatusUpdateCallback& callback);

 private:
  friend class InUseAuthSession;

  // Starts/Restarts/Stops the expiration timer based on the current contents of
  // the expiration map.
  void ResetExpirationTimer();

  // Callback registered with sessions to catch authentication. This will set
  // the session to timeout in kAuthTimeout.
  void SessionOnAuthCallback(const base::UnguessableToken& token);

  // Callback to flush any expired sessions in the expiration map.
  void ExpireAuthSessions();

  // Run as the destructor for InUseAuthSession, signaling that any active dbus
  // calls that referenced the AuthSession have now finished.
  void MarkNotInUse(std::unique_ptr<AuthSession> session);

  // The underlying backing APIs used to construct new sessions.
  AuthSession::BackingApis backing_apis_;

  // The repeating callback to send AuthFactorStatusUpdateSignal.
  AuthFactorStatusUpdateCallback auth_factor_status_update_callback_;

  // Track all of the managed auth sessions by token. For AuthSessions in active
  // use, the unique_ptr for the AuthSession for a given token will be nullptr,
  // as the ownership is being held by an InUseAuthSession object.
  std::map<base::UnguessableToken, std::unique_ptr<AuthSession>> auth_sessions_;

  // Timer infrastructure used to track sessions for expiration. This is done by
  // using a map of expiration time -> token to keep track of when sessions
  // along with a timer which will be triggered whenever at least one of this
  // sessions is ready to be expired.
  //
  // Note that this needs to be a multimap because you can have multiple
  // sessions that are set to expire at the exact same time.
  std::multimap<base::Time, base::UnguessableToken> expiration_map_;
  const base::Clock* clock_;
  base::WallClockTimer expiration_timer_;
};

// The InUseAuthSession class is a wrapper around AuthSession that indicates
// that a managed session is currently "in use". This wrapper receives ownership
// of the session from the session manager when it is constructed, and then it
// returns ownership back when it is destroyed.
//
// This is used to prevent multiple operations from attempting to use the same
// session at the same time. Normally the implementation of a dbus operation
// will use FindAuthSession to get the session it is running on, storing the
// returned InUseAuthSession into a local variable. When the operation
// terminates and the local InUseAuthSession is destroyed, the session will go
// back to the manager and again be available for other operations to find.
//
// This object behaves similarly to a StatusOk<AuthSession>. It can have a
// not-OK status (via AuthSessionStatus()) to indicate that there is not a valid
// underlying AuthSession object, and it provides deference operators (* and ->)
// for accessing said object when it IS valid.
class InUseAuthSession {
 public:
  InUseAuthSession();

  InUseAuthSession(InUseAuthSession&& auth_session);
  InUseAuthSession& operator=(InUseAuthSession&& auth_session);

  ~InUseAuthSession();

  // Pointer operators.
  AuthSession& operator*() { return *session_; }
  const AuthSession& operator*() const { return *session_; }
  AuthSession* operator->() { return session_.get(); }
  const AuthSession* operator->() const { return session_.get(); }
  AuthSession* Get() { return session_.get(); }
  const AuthSession* Get() const { return session_.get(); }

  // Indicates the status of the in-use object. This is set to not-OK when the
  // object does not contain a valid underlying session.
  CryptohomeStatus AuthSessionStatus() const;

  // The remaining lifetime of this session before it is expired. Note that it
  // is possible for this to return zero; even in that case the session is not
  // actually considered to be expired until the session is deleted.
  base::TimeDelta GetRemainingTime() const;

  // Extends the timer for the AuthSession by specified duration. Note that this
  // can fail, in which case a not-OK status will returned.
  CryptohomeStatus ExtendTimeout(base::TimeDelta extension);

 private:
  friend class AuthSessionManager;

  InUseAuthSession(AuthSessionManager& manager,
                   bool is_session_active,
                   std::unique_ptr<AuthSession> session);

  AuthSessionManager* manager_;
  bool is_session_active_;
  std::unique_ptr<AuthSession> session_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_MANAGER_H_

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_MANAGER_H_
#define CRYPTOHOME_AUTH_SESSION_MANAGER_H_

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>

#include <base/memory/weak_ptr.h>
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
class BoundAuthSession;

class AuthSessionManager {
 public:
  // The default timeout duration for sessions.
  static constexpr base::TimeDelta kAuthTimeout = base::Minutes(5);

  // Construct a session manager that will use the given backing APIs to create
  // new AuthSession objects.
  explicit AuthSessionManager(AuthSession::BackingApis backing_apis);

  AuthSessionManager(AuthSessionManager&) = delete;
  AuthSessionManager& operator=(AuthSessionManager&) = delete;

  // Creates new auth session for account_id with the specified flags and
  // intent. Returns the token for the newly created session.
  base::UnguessableToken CreateAuthSession(const Username& account_id,
                                           uint32_t flags,
                                           AuthIntent auth_intent);
  // Allow for the explicit control over the AuthSession parameters. This should
  // generally only be used in testing.
  base::UnguessableToken CreateAuthSession(
      AuthSession::Params auth_session_params);

  // Removes existing auth session with token. Returns false if there's no auth
  // session with this token.
  bool RemoveAuthSession(const base::UnguessableToken& token);
  // Overload for remove to avoid deserialization client side.
  bool RemoveAuthSession(const std::string& serialized_token);

  // Removes all the authsession and calls their destructor. This is supposed to
  // be used when UnMountall() API is called.
  void RemoveAllAuthSessions();

  // Used to set the auth factor status update callback inside class so it could
  // be passed to each auth session.
  void SetAuthFactorStatusUpdateCallback(
      const AuthFactorStatusUpdateCallback& callback);

  // Finds existing auth session with token and invoke |callback| with the auth
  // session. If the auth session is available or doesn't exist, the callback is
  // invoked immediately. If the auth session exists but is currently active,
  // |callback| will be invoked when the auth session becomes available
  // (released from active usage).
  void RunWhenAvailable(const base::UnguessableToken& token,
                        base::OnceCallback<void(InUseAuthSession)> callback);
  // Overload to avoid deserialization on client side.
  void RunWhenAvailable(const std::string& serialized_token,
                        base::OnceCallback<void(InUseAuthSession)> callback);

 private:
  friend class InUseAuthSession;
  friend class BoundAuthSession;

  // Represents an instance of pending work scheduled for an auth session. If
  // the work object is destroyed before it has been executed then the work
  // callback will be called with an error.
  class PendingWork {
   public:
    using Callback = base::OnceCallback<void(InUseAuthSession)>;

    PendingWork(base::UnguessableToken session_token, Callback work_callback);

    // Pending work objects can be moved but not copied, because the underlying
    // work callback cannot be copied. A moved-from work object is considered to
    // be in a null state and can only be assigned to or destroyed.
    PendingWork(const PendingWork&) = delete;
    PendingWork& operator=(const PendingWork&) = delete;
    PendingWork(PendingWork&& other);
    PendingWork& operator=(PendingWork&& other);

    ~PendingWork();

    const base::UnguessableToken& session_token() const {
      return session_token_;
    }

    // Execute the pending work against the given session. Attempting to run
    // this work twice is a CHECK failure.
    void Run(InUseAuthSession session) &&;

   private:
    // Token that identifies the session this work is targeted to.
    base::UnguessableToken session_token_;
    // The work callback. Once the callback is executed this will be cleared.
    std::optional<Callback> work_callback_;
  };

  // Add a new session. Implements the common portion of the CreateAuthSession
  // calls, after the session has successfully been created.
  base::UnguessableToken AddAuthSession(
      std::unique_ptr<AuthSession> auth_session);

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

  // Map of session tokens to the session user.
  std::map<base::UnguessableToken, ObfuscatedUsername> token_to_user_;

  // For each user, stores all of their sessions as well as a work queue.
  struct UserAuthSessions {
    // All of the auth sessions for this user. If one of the sessions is in
    // active use then it will still have an entry in this map but the value
    // will be nullptr with the ownership being held by an InUseAuthSession.
    std::map<base::UnguessableToken, std::unique_ptr<AuthSession>>
        auth_sessions;
    // A queue of pending work for the session.
    std::queue<PendingWork> work_queue;
    // Populated with the token of the currently in use session if that session
    // is removed while it is in use.
    std::optional<base::UnguessableToken> zombie_session;
  };
  std::map<ObfuscatedUsername, UserAuthSessions> user_auth_sessions_;

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

  // The last member, to invalidate weak references first on destruction.
  base::WeakPtrFactory<AuthSessionManager> weak_factory_{this};
};

// The InUseAuthSession class is a wrapper around AuthSession that indicates
// that a managed session is currently "in use". This wrapper receives ownership
// of the session from the session manager when it is constructed, and then it
// returns ownership back when it is destroyed.
//
// Conceptually, this is similar to a smart pointer but instead of signalling "I
// own this session" it signals "I am using this session". Destroying the InUse
// object signals that you are no longer using the session and makes it
// available for use by others, rather than terminating the session.
//
// Normally the implementation of a dbus operation will use RunWhenAvailable to
// schedule work (via a callback) against the session when it is not busy. The
// callback will be given an InUseAuthSession which it can do work against and
// then release upon completion to make the session available again for other
// callbacks and operations.
//
// This object behaves similarly to a StatusOk<AuthSession>. It can have a
// not-OK status (via AuthSessionStatus()) to indicate that there is not a valid
// underlying AuthSession object, and it provides dereference operators (* and
// ->) for accessing said object when it IS valid.
class InUseAuthSession {
 public:
  InUseAuthSession();

  InUseAuthSession(InUseAuthSession&& auth_session);
  InUseAuthSession& operator=(InUseAuthSession&& auth_session);

  ~InUseAuthSession();

  // Pointer operators. Note that the references and pointers to the AuthSession
  // returned by these are only guaranteed to be valid so long as the
  // InUseAuthSession that they came from is live.
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

  // Convert the in-use object into a bound one for use in a callback. Note that
  // it is only safe to use this if the functions being used with a callback
  // check AuthSessionStatus again once they execute, as a formerly valid in-use
  // object may have been timed out.
  std::unique_ptr<BoundAuthSession> BindForCallback() &&;

 private:
  friend class AuthSessionManager;
  friend class BoundAuthSession;

  InUseAuthSession(AuthSessionManager& manager,
                   std::unique_ptr<AuthSession> session);

  AuthSessionManager* manager_;
  std::unique_ptr<AuthSession> session_;
};

// Wrapper class that can be used to more safely bind an in-use AuthSession to a
// callback. Note that in general functions should not accept this object
// directly as a parameter; it will get automatically constructed by
// BindForCallback and will automatically be unwrapped back into an
// InUseAuthSession when the callback is called.
//
// Motivations for this class:
//
// While in theory you could just bind an InUseAuthSession object directly to a
// callback as it is a moveable object, this can be dangerous because an in-use
// object blocks all subsequent session operations for a user and so if the
// callback is never called then that user will be blocked "forever".
//
// This problem could happen with any InUseAuthSession object but is much less
// likely when it is only being used as a local variable. Local variables will
// be destroyed when the scope is exited and in practice "this function never
// returns" bugs are less common than "this async event never happens".
//
// This object provides some safety by setting a timeout which will release the
// session if it is blocking any other operations. This ensures that a session
// bound to a callback will not block a user indefinitely.
class BoundAuthSession {
 public:
  // The timeouts used by the session.
  static constexpr base::TimeDelta kTimeout = base::Minutes(1);
  static constexpr base::TimeDelta kShortTimeout = base::Seconds(10);

  explicit BoundAuthSession(InUseAuthSession auth_session);

  // Bound sessions are explicitly not movable because they need to register
  // a callback against themselves for timeout and moving them around would
  // break that. To actually bind a bound session into a callback you need to
  // also wrap it in a unique_ptr.
  BoundAuthSession(BoundAuthSession&) = delete;
  BoundAuthSession& operator=(BoundAuthSession&) = delete;

  // Return the in use session. The callers must check the returned object for
  // validity before using the session.
  InUseAuthSession Take() &&;

 private:
  // If the session being in use is blocking any work, release it back to the
  // manager. Otherwise reset the timeout timer to check again later.
  void ReleaseSessionIfBlocking();

  // Schedule a release-if-blocking check in the given time delta. The caller
  // must ensure that the session is OK before calling this.
  void ScheduleReleaseCheck(base::TimeDelta delay);

  InUseAuthSession session_;
  base::WallClockTimer timeout_timer_;
};

}  // namespace cryptohome

namespace base {
// Add a always-fails unwrap template for InUseAuthSession. Normally these are
// supposed used to unwrap wrapping/ptr types, but here we use this as a trick
// to fail compilation if anyone tries to bind an in-use object to a callback.
template <>
struct BindUnwrapTraits<cryptohome::InUseAuthSession> {
  template <typename T>
  static T Unwrap(T o) {
    static_assert(false, "InUseAuthSession is not safe to bind to callbacks");
    return o;
  }
};
// When a callback bound to a BoundAuthSession is called, convert the bound
// value into an InUseAuthSession for the receiver.
template <>
struct BindUnwrapTraits<std::unique_ptr<cryptohome::BoundAuthSession>> {
  static cryptohome::InUseAuthSession Unwrap(
      std::unique_ptr<cryptohome::BoundAuthSession> o) {
    return std::move(*o).Take();
  }
};
}  // namespace base

#endif  // CRYPTOHOME_AUTH_SESSION_MANAGER_H_

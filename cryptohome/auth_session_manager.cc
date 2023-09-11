// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/notreached.h>
#include <base/time/default_clock.h>
#include <base/time/time.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/status.h>

#include "cryptohome/error/location_utils.h"
#include "cryptohome/platform.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

}  // namespace

AuthSessionManager::AuthSessionManager(AuthSession::BackingApis backing_apis)
    : backing_apis_(backing_apis), clock_(base::DefaultClock::GetInstance()) {
  CHECK(backing_apis.crypto);
  CHECK(backing_apis.platform);
  CHECK(backing_apis.user_session_map);
  CHECK(backing_apis.keyset_management);
  CHECK(backing_apis.auth_block_utility);
  CHECK(backing_apis.auth_factor_driver_manager);
  CHECK(backing_apis.auth_factor_manager);
  CHECK(backing_apis.user_secret_stash_storage);
  CHECK(backing_apis.user_metadata_reader);
  CHECK(backing_apis.features);
}

CryptohomeStatusOr<InUseAuthSession> AuthSessionManager::CreateAuthSession(
    const Username& account_id, uint32_t flags, AuthIntent auth_intent) {
  // Assumption here is that keyset_management_ will outlive this AuthSession.
  std::unique_ptr<AuthSession> auth_session =
      AuthSession::Create(account_id, flags, auth_intent, backing_apis_);
  return AddAuthSession(std::move(auth_session));
}

InUseAuthSession AuthSessionManager::AddAuthSession(
    std::unique_ptr<AuthSession> auth_session) {
  // We should never, ever, be able to get a token collision.
  const auto& token = auth_session->token();
  auto iter = auth_sessions_.lower_bound(token);
  CHECK(iter == auth_sessions_.end() || iter->first != token)
      << "AuthSession token collision";

  // Add an entry to the session map. Note that we're deliberately initializing
  // things into an in-use state by only adding a blank entry in the map.
  auth_sessions_.emplace_hint(iter, token, AuthSessionMapEntry{});
  InUseAuthSession in_use(*this, /*is_session_active=*/true,
                          std::move(auth_session));

  // Add an expiration entry for the session set to the end of time.
  base::Time expiration_time = base::Time::Max();
  expiration_map_.emplace(expiration_time, token);
  ResetExpirationTimer();

  // Attach the OnAuth handler to the AuthSession. It's important that we do
  // this after creating the map entries and in_use object because the callback
  // may immediately fire.
  //
  // Note that it is safe for use to use |Unretained| here because the manager
  // should always outlive all of the sessions it owns.
  in_use->AddOnAuthCallback(
      base::BindOnce(&AuthSessionManager::SessionOnAuthCallback,
                     base::Unretained(this), token));

  // Set the AuthFactorStatusUpdate signal handler to the auth session.
  if (auth_factor_status_update_callback_) {
    in_use->SetAuthFactorStatusUpdateCallback(
        base::BindRepeating(auth_factor_status_update_callback_));
    in_use->SendAuthFactorStatusUpdateSignal();
  }

  return in_use;
}

void AuthSessionManager::RemoveAllAuthSessions() {
  auth_sessions_.clear();
  expiration_map_.clear();
}

bool AuthSessionManager::RemoveAuthSession(
    const base::UnguessableToken& token) {
  // Remove the session from the expiration map. If we don't find an entry we
  // ignore this and rely on the session map removal step to catch the error.
  for (auto iter = expiration_map_.begin(); iter != expiration_map_.end();
       ++iter) {
    if (iter->second == token) {
      expiration_map_.erase(iter);
      break;
    }
  }
  // Remove the session from the session map.
  return auth_sessions_.erase(token) == 1;
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

InUseAuthSession AuthSessionManager::FindAuthSession(
    const std::string& serialized_token) {
  std::optional<base::UnguessableToken> token =
      AuthSession::GetTokenFromSerializedString(serialized_token);
  if (!token.has_value()) {
    LOG(ERROR) << "Unparsable AuthSession token for find";
    return InUseAuthSession(*this, /*is_session_active=*/false, nullptr);
  }
  return FindAuthSession(token.value());
}

InUseAuthSession AuthSessionManager::FindAuthSession(
    const base::UnguessableToken& token) {
  auto it = auth_sessions_.find(token);
  if (it == auth_sessions_.end()) {
    return InUseAuthSession(*this, /*is_session_active=*/false, nullptr);
  }

  // If the AuthSessionManager doesn't own the AuthSession unique_ptr,
  // then the AuthSession is actively in use for another dbus operation.
  if (!it->second.session) {
    return InUseAuthSession(*this, /*is_session_active=*/true, nullptr);
  } else {
    // By giving ownership of the unique_ptr we are marking
    // the AuthSession as in active use.
    return InUseAuthSession(*this, /*is_session_active=*/false,
                            std::move(it->second.session));
  }
}

void AuthSessionManager::ResetExpirationTimer() {
  if (expiration_map_.empty()) {
    expiration_timer_.Stop();
  } else {
    expiration_timer_.Start(
        FROM_HERE, expiration_map_.cbegin()->first,
        base::BindOnce(&AuthSessionManager::ExpireAuthSessions,
                       base::Unretained(this)));
  }
}

void AuthSessionManager::SessionOnAuthCallback(
    const base::UnguessableToken& token) {
  // Find the existing expiration time of the session.
  auto iter = expiration_map_.begin();
  while (iter != expiration_map_.end() && iter->second != token) {
    ++iter;
  }
  // If we couldn't find a session something really went wrong, but there's not
  // much we can do about it.
  if (iter == expiration_map_.end()) {
    LOG(ERROR) << "AuthSessionManager received an OnAuth event for a session "
                  "which it is not managing";
    return;
  }
  // Remove the existing expiration entry and add a new one that triggers
  // starting now.
  base::Time new_time = clock_->Now() + kAuthTimeout;
  expiration_map_.erase(iter);
  expiration_map_.emplace(new_time, token);
  ResetExpirationTimer();
}

void AuthSessionManager::ExpireAuthSessions() {
  base::Time now = clock_->Now();
  // Go through the map, removing all of the sessions until we find one with an
  // expiration time after now (or reach the end).
  //
  // This will always remove the first element of the map even if its expiration
  // time is later than now. This is because it's possible for the timer to be
  // triggered slightly early and we don't want this callback to turn into a
  // busy-wait where it runs over and over as a no-op.
  auto iter = expiration_map_.begin();
  bool first_entry = true;
  while (iter != expiration_map_.end() && (first_entry || iter->first <= now)) {
    if (auth_sessions_.erase(iter->second) == 0) {
      LOG(FATAL) << "AuthSessionManager expired a session it is not managing";
    }
    ++iter;
    first_entry = false;
  }
  // Erase all of the entries from the map that were just removed.
  iter = expiration_map_.erase(expiration_map_.begin(), iter);
  // Reset the expiration timer to run again based on what's left in the map.
  ResetExpirationTimer();
}

void AuthSessionManager::MarkNotInUse(std::unique_ptr<AuthSession> session) {
  // If the session token still exists in the session map then return ownership
  // of the session back to the manager.
  auto it = auth_sessions_.find(session->token());
  if (it == auth_sessions_.end()) {
    // If it doesn't exist then that means the session has been removed and so
    // just return and allow the object to be destroyed.
    return;
  }
  if (!it->second.pending_callbacks.IsEmpty()) {
    it->second.pending_callbacks.Pop().Run(InUseAuthSession(
        *this, /*is_session_active=*/false, std::move(session)));
    return;
  }
  it->second.session = std::move(session);
}

void AuthSessionManager::SetAuthFactorStatusUpdateCallback(
    const AuthFactorStatusUpdateCallback& callback) {
  auth_factor_status_update_callback_ = callback;
}

void AuthSessionManager::RunWhenAvailable(
    const std::string& serialized_token,
    base::OnceCallback<void(InUseAuthSession)> callback) {
  std::optional<base::UnguessableToken> token =
      AuthSession::GetTokenFromSerializedString(serialized_token);
  if (!token.has_value()) {
    LOG(ERROR) << "Unparsable AuthSession token for find";
    std::move(callback).Run(
        InUseAuthSession(*this, /*is_session_active=*/false, nullptr));
  }
  RunWhenAvailable(token.value(), std::move(callback));
}

void AuthSessionManager::RunWhenAvailable(
    const base::UnguessableToken& token,
    base::OnceCallback<void(InUseAuthSession)> callback) {
  auto it = auth_sessions_.find(token);
  if (it == auth_sessions_.end()) {
    std::move(callback).Run(
        InUseAuthSession(*this, /*is_session_active=*/false, nullptr));
    return;
  }

  // If the AuthSessionManager doesn't own the AuthSession unique_ptr,
  // then the AuthSession is actively in use for another dbus operation. Put the
  // callback into the pending_callbacks queue.
  if (!it->second.session) {
    it->second.pending_callbacks.Push(std::move(callback));
    return;
  }

  // By giving ownership of the unique_ptr we are marking
  // the AuthSession as in active use.
  std::move(callback).Run(InUseAuthSession(*this, /*is_session_active=*/false,
                                           std::move(it->second.session)));
}

AuthSessionManager::PendingCallbacksQueue::~PendingCallbacksQueue() {
  while (!IsEmpty()) {
    Pop().Run(InUseAuthSession());
  }
}

bool AuthSessionManager::PendingCallbacksQueue::IsEmpty() {
  return callbacks_.empty();
}

void AuthSessionManager::PendingCallbacksQueue::Push(
    base::OnceCallback<void(InUseAuthSession)> callback) {
  callbacks_.push(std::move(callback));
}

base::OnceCallback<void(InUseAuthSession)>
AuthSessionManager::PendingCallbacksQueue::Pop() {
  base::OnceCallback<void(InUseAuthSession)> callback =
      std::move(callbacks_.front());
  callbacks_.pop();
  return callback;
}

InUseAuthSession::InUseAuthSession()
    : manager_(nullptr), is_session_active_(false), session_(nullptr) {}

InUseAuthSession::InUseAuthSession(AuthSessionManager& manager,
                                   bool is_session_active,
                                   std::unique_ptr<AuthSession> session)
    : manager_(&manager),
      is_session_active_(is_session_active),
      session_(std::move(session)) {}

InUseAuthSession::InUseAuthSession(InUseAuthSession&& auth_session)
    : manager_(auth_session.manager_),
      is_session_active_(auth_session.is_session_active_),
      session_(std::move(auth_session.session_)) {}

InUseAuthSession& InUseAuthSession::operator=(InUseAuthSession&& auth_session) {
  manager_ = auth_session.manager_;
  is_session_active_ = auth_session.is_session_active_;
  session_ = std::move(auth_session.session_);
  return *this;
}

InUseAuthSession::~InUseAuthSession() {
  if (session_ && manager_) {
    manager_->MarkNotInUse(std::move(session_));
  }
}

CryptohomeStatus InUseAuthSession::AuthSessionStatus() const {
  if (!session_) {
    // InUseAuthSession wasn't made with a valid AuthSession unique_ptr
    if (is_session_active_) {
      LOG(ERROR) << "Existing AuthSession is locked in a previous opertaion.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionManagerAuthSessionActive),
          ErrorActionSet({PossibleAction::kReboot}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    } else {
      LOG(ERROR) << "Invalid AuthSession token provided.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthSessionManagerAuthSessionNotFound),
          ErrorActionSet({PossibleAction::kReboot}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
    }
  } else {
    return OkStatus<CryptohomeError>();
  }
}

base::TimeDelta InUseAuthSession::GetRemainingTime() const {
  // Find the expiration time of the session. If it doesn't have one then its
  // expiration is pending the object no longer being in use, which we report as
  // zero remaining time.
  std::optional<base::Time> expiration_time;
  for (const auto& [time, token] : manager_->expiration_map_) {
    if (token == session_->token()) {
      expiration_time = time;
      break;
    }
  }
  if (!expiration_time) {
    return base::TimeDelta();
  }
  // If the expiration time is the end of time, then report the max duration.
  if (expiration_time->is_max()) {
    return base::TimeDelta::Max();
  }
  // Given the (finite) expiration time we now have, compute the remaining time.
  // If the expiration time is in the past (e.g. because the expiration timer
  // hasn't fired yet) then we clamp the time to zero.
  base::TimeDelta time_left = *expiration_time - manager_->clock_->Now();
  return time_left.is_negative() ? base::TimeDelta() : time_left;
}

CryptohomeStatus InUseAuthSession::ExtendTimeout(base::TimeDelta extension) {
  // Find the existing expiration time of the session. If it doesn't have one
  // then the session has already been expired pending the session no longer
  // being in use. This cannot be reverted and so the extend fails.
  auto iter = manager_->expiration_map_.begin();
  while (iter != manager_->expiration_map_.end() &&
         iter->second != session_->token()) {
    ++iter;
  }
  if (iter == manager_->expiration_map_.end()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionTimedOutInExtend),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kRetry,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  }
  // Remove the existing expiration entry and add a new one with the new time.
  base::Time new_time = iter->first + extension;
  manager_->expiration_map_.erase(iter);
  manager_->expiration_map_.emplace(new_time, session_->token());
  manager_->ResetExpirationTimer();
  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome

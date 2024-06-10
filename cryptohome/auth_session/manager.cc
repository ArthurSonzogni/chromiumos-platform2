// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session/manager.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/notreached.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/default_clock.h>
#include <base/time/time.h>
#include <base/unguessable_token.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/status.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

}  // namespace

AuthSessionManager::AuthSessionManager(AuthSession::BackingApis backing_apis,
                                       base::SequencedTaskRunner* task_runner)
    : backing_apis_(backing_apis),
      task_runner_(task_runner),
      clock_(base::DefaultClock::GetInstance()) {
  CHECK(backing_apis.crypto);
  CHECK(backing_apis.platform);
  CHECK(backing_apis.user_session_map);
  CHECK(backing_apis.keyset_management);
  CHECK(backing_apis.auth_block_utility);
  CHECK(backing_apis.auth_factor_driver_manager);
  CHECK(backing_apis.auth_factor_manager);
  CHECK(backing_apis.user_secret_stash_storage);
  CHECK(backing_apis.features);
}

base::UnguessableToken AuthSessionManager::CreateAuthSession(
    Username account_id, AuthSession::CreateOptions options) {
  std::unique_ptr<AuthSession> auth_session = AuthSession::Create(
      std::move(account_id), std::move(options), backing_apis_);
  return AddAuthSession(std::move(auth_session));
}

base::UnguessableToken AuthSessionManager::CreateAuthSession(
    AuthSession::Params auth_session_params) {
  return AddAuthSession(std::make_unique<AuthSession>(
      std::move(auth_session_params), backing_apis_));
}

bool AuthSessionManager::RemoveAuthSession(
    const base::UnguessableToken& token) {
  // Remove the session from the expiration map. If we don't find an entry we
  // ignore this and still try to remove the underlying session.
  bool auth_session_removed = false;
  {
    auto iter = expiration_map_.begin();
    while (iter != expiration_map_.end()) {
      if (iter->second == token) {
        expiration_map_.erase(iter);
        auth_session_removed = true;
        break;
      }
      ++iter;
    }
  }

  // If the entry wasn't in the expiration map it might be in the expiring soon
  // map, so check it as well.
  if (!auth_session_removed) {
    auto expiring_iter = auth_session_expiring_soon_map_.begin();
    while (expiring_iter != auth_session_expiring_soon_map_.end()) {
      if (expiring_iter->second == token) {
        auth_session_expiring_soon_map_.erase(expiring_iter);
        auth_session_removed = true;
        break;
      }
      ++expiring_iter;
    }
  }

  if (!auth_session_removed) {
    return false;
  }

  // In case anything is removed, reset the timer. If nothing is removed, it'll
  // just reset the timer and would essentially be a no-op.
  ResetExpirationTimer();
  // Find entries for the token in the token and user
  // maps. If any of the lookups fail we report an error.
  auto token_iter = token_to_user_.find(token);
  if (token_iter == token_to_user_.end()) {
    return false;
  }
  auto user_iter = user_auth_sessions_.find(token_iter->second);
  if (user_iter == user_auth_sessions_.end()) {
    return false;
  }
  auto session_iter = user_iter->second.auth_sessions.find(token);
  if (session_iter == user_iter->second.auth_sessions.end()) {
    return false;
  }
  // If we get here we found all the entries for this session, remove them all
  // and report success. If the session is in use, also mark is as the zombie
  // session so that we know the user is still busy.
  if (session_iter->second == nullptr) {
    user_iter->second.zombie_session = token;
  }
  user_iter->second.auth_sessions.erase(session_iter);
  if (!user_iter->second.zombie_session.has_value() &&
      user_iter->second.auth_sessions.empty()) {
    user_auth_sessions_.erase(user_iter);
  }
  token_to_user_.erase(token_iter);
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

void AuthSessionManager::RemoveUserAuthSessions(
    const ObfuscatedUsername& username) {
  std::set<base::UnguessableToken> tokens_being_removed;
  for (auto iter = token_to_user_.begin(); iter != token_to_user_.end();) {
    if (iter->second == username) {
      tokens_being_removed.insert(iter->first);
      iter = token_to_user_.erase(iter);
    } else {
      ++iter;
    }
  }
  user_auth_sessions_.erase(username);
  for (auto iter = expiration_map_.begin(); iter != expiration_map_.end();) {
    if (tokens_being_removed.contains(iter->second)) {
      iter = expiration_map_.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = auth_session_expiring_soon_map_.begin();
       iter != auth_session_expiring_soon_map_.end();) {
    if (tokens_being_removed.contains(iter->second)) {
      iter = auth_session_expiring_soon_map_.erase(iter);
    } else {
      ++iter;
    }
  }
  ResetExpirationTimer();
}

void AuthSessionManager::RemoveAllAuthSessions() {
  token_to_user_.clear();
  user_auth_sessions_.clear();
  expiration_map_.clear();
  auth_session_expiring_soon_map_.clear();
  ResetExpirationTimer();
}

void AuthSessionManager::RunWhenAvailable(
    const base::UnguessableToken& token,
    base::OnceCallback<void(InUseAuthSession)> callback,
    const base::Location& from_here) {
  PendingWork work(token, from_here, std::move(callback), task_runner_);

  // Look up the user sessions instance for the given token. If it doesn't exist
  // just execute the callback immediately with an invalid InUse object.
  auto token_iter = token_to_user_.find(token);
  if (token_iter == token_to_user_.end()) {
    return;
  }
  auto user_iter = user_auth_sessions_.find(token_iter->second);
  if (user_iter == user_auth_sessions_.end()) {
    return;
  }

  // Check if the user is busy, i.e. if they have any sessions that are
  // currently in use. If they are, add an item to the pending work queue.
  if (user_iter->second.zombie_session.has_value()) {
    user_iter->second.work_queue.push(std::move(work));
    return;
  }
  for (const auto& [unused_token, session] : user_iter->second.auth_sessions) {
    if (!session) {
      user_iter->second.work_queue.push(std::move(work));
      return;
    }
  }

  // If we get here then the user is not busy, execute the callback immediately.
  auto session_iter = user_iter->second.auth_sessions.find(token);
  if (session_iter == user_iter->second.auth_sessions.end()) {
    return;
  }
  std::move(work).Run(InUseAuthSession(*this, std::move(session_iter->second)));
}

void AuthSessionManager::RunWhenAvailable(
    const std::string& serialized_token,
    base::OnceCallback<void(InUseAuthSession)> callback,
    const base::Location& from_here) {
  std::optional<base::UnguessableToken> token =
      AuthSession::GetTokenFromSerializedString(serialized_token);
  if (!token.has_value()) {
    LOG(ERROR) << "Unparsable AuthSession token for find";
    std::move(callback).Run(InUseAuthSession());
    return;
  }
  RunWhenAvailable(token.value(), std::move(callback), from_here);
}

base::UnguessableToken AuthSessionManager::AddAuthSession(
    std::unique_ptr<AuthSession> auth_session) {
  // Find the insertion location in the token->user map. We should never, ever,
  // be able to get a token collision.
  const auto token = auth_session->token();
  const ObfuscatedUsername username = auth_session->obfuscated_username();
  auto token_iter = token_to_user_.lower_bound(token);
  CHECK(token_iter == token_to_user_.end() || token_iter->first != token)
      << "AuthSession token collision";

  // Find the insertion location in the user->session map. This may create a new
  // map implicitly if this is the first session for this user. Again, we should
  // never, ever be able to get a token collision.
  auto& user_entry = user_auth_sessions_[username];
  auto session_iter = user_entry.auth_sessions.lower_bound(token);
  CHECK(session_iter == user_entry.auth_sessions.end() ||
        session_iter->first != token)
      << "AuthSession token collision";

  // Add entries to both maps.
  token_to_user_.emplace_hint(token_iter, token, username);
  session_iter = user_entry.auth_sessions.emplace_hint(session_iter, token,
                                                       std::move(auth_session));
  AuthSession& added_session = *session_iter->second;

  // Add an expiration entry for the session set to the end of time.
  base::Time expiration_time = base::Time::Max();
  expiration_map_.emplace(expiration_time, token);
  ResetExpirationTimer();

  // Trigger a status update for the newly added session.
  added_session.SendAuthFactorStatusUpdateSignal();

  // Attach the OnAuth handler to the AuthSession. It's important that we do
  // this after creating the map entries because the callback may immediately
  // fire. We should also avoid touching the session after this because it's
  // technically possible for it to have been destroyed.
  added_session.AddOnAuthCallback(
      base::BindOnce(&AuthSessionManager::SessionOnAuthCallback,
                     weak_factory_.GetWeakPtr(), token));

  return token;
}

void AuthSessionManager::MoveAuthSessionsToExpiringSoon() {
  auto iter = expiration_map_.begin();
  bool need_moving = false;
  while (iter != expiration_map_.end() &&
         (iter->first - clock_->Now()) <= kAuthTimeoutWarning) {
    auto token_iter = token_to_user_.find(iter->second);
    if (token_iter == token_to_user_.end()) {
      continue;
    }
    // If the sending the signal fails or is not sent because of authsession not
    // found, that's fine for now since this is informational.
    if (user_auth_sessions_.find(token_to_user_[iter->second]) !=
            user_auth_sessions_.end() &&
        backing_apis_.signalling) {
      user_data_auth::AuthSessionExpiring expiring_proto;
      auto broadcast_id =
          user_auth_sessions_.find(token_to_user_[iter->second])
              ->second.auth_sessions[iter->second]
              ->serialized_public_token();
      expiring_proto.set_broadcast_id(broadcast_id);
      expiring_proto.set_time_left((iter->first - clock_->Now()).InSeconds());
      backing_apis_.signalling->SendAuthSessionExpiring(expiring_proto);
    }
    ++iter;
    need_moving = true;
  }

  if (need_moving) {
    std::copy(std::make_move_iterator(expiration_map_.begin()),
              std::make_move_iterator(iter),
              std::inserter(auth_session_expiring_soon_map_,
                            auth_session_expiring_soon_map_.begin()));
    // Clearing it explicitly in order to avoid a undefined state.
    expiration_map_.erase(expiration_map_.cbegin(), iter);
  }
  ResetExpirationTimer();
}

void AuthSessionManager::ResetExpirationTimer() {
  if (auth_session_expiring_soon_map_.empty() && expiration_map_.empty()) {
    expiration_timer_.Stop();
    return;
  }

  if (auth_session_expiring_soon_map_.empty() ||
      (!expiration_map_.empty() &&
       ((expiration_map_.cbegin()->first - kAuthTimeoutWarning) <
        auth_session_expiring_soon_map_.cbegin()->first))) {
    expiration_timer_.Start(
        FROM_HERE, expiration_map_.cbegin()->first - kAuthTimeoutWarning,
        base::BindOnce(&AuthSessionManager::MoveAuthSessionsToExpiringSoon,
                       weak_factory_.GetWeakPtr()));
    return;
  }
  expiration_timer_.Start(
      FROM_HERE, auth_session_expiring_soon_map_.cbegin()->first,
      base::BindOnce(&AuthSessionManager::ExpireAuthSessions,
                     weak_factory_.GetWeakPtr()));
}

void AuthSessionManager::SessionOnAuthCallback(
    const base::UnguessableToken& token) {
  // Find the existing expiration time of the session.
  auto iter = expiration_map_.begin();
  while (iter != expiration_map_.end() && iter->second != token) {
    ++iter;
  }
  // If we couldn't find a session then check the expiring soon map.
  if (iter == expiration_map_.end()) {
    CheckExpiringSoonMap(token);
    return;
  }
  // Remove the existing expiration entry and add a new one that triggers
  // starting now.
  base::Time new_time = clock_->Now() + kAuthTimeout;
  expiration_map_.erase(iter);
  expiration_map_.emplace(new_time, token);
  ResetExpirationTimer();
}

void AuthSessionManager::CheckExpiringSoonMap(
    const base::UnguessableToken& token) {
  // Find the existing expiration time of the session.
  auto iter = auth_session_expiring_soon_map_.begin();
  while (iter != auth_session_expiring_soon_map_.end() &&
         iter->second != token) {
    ++iter;
  }
  // If we couldn't find a session something really went wrong, but there's not
  // much we can do about it.
  if (iter == auth_session_expiring_soon_map_.end()) {
    LOG(ERROR) << "AuthSessionManager received an OnAuth event for a session "
                  "which it is not managing";
    return;
  }
  // Remove the existing expiration entry and add a new one that triggers
  // starting now.
  base::Time new_time = clock_->Now() + kAuthTimeout;
  auth_session_expiring_soon_map_.erase(iter);
  // We add it to the new map.
  expiration_map_.emplace(new_time, token);
  ResetExpirationTimer();
}

void AuthSessionManager::ExpireAuthSessions() {
  base::Time now = clock_->Now();
  // Go through the map, removing all of the sessions until we find one with
  // an expiration time after now (or reach the end).
  //
  // This will always remove the first element of the map even if its
  // expiration time is later than now. This is because it's possible for the
  // timer to be triggered slightly early and we don't want this callback to
  // turn into a busy-wait where it runs over and over as a no-op.
  auto iter = auth_session_expiring_soon_map_.begin();
  int expired_sessions = 0;
  while (iter != auth_session_expiring_soon_map_.end() &&
         (expired_sessions == 0 || iter->first <= now)) {
    auto token_iter = token_to_user_.find(iter->second);
    if (token_iter == token_to_user_.end()) {
      LOG(FATAL) << "token_iter: AuthSessionManager expired a session it is "
                    "not managing";
    }
    auto user_iter = user_auth_sessions_.find(token_iter->second);
    if (user_iter == user_auth_sessions_.end()) {
      LOG(FATAL) << "user_iter:AuthSessionManager expired a session it "
                    "is not managing";
    }
    auto session_iter = user_iter->second.auth_sessions.find(iter->second);
    if (session_iter == user_iter->second.auth_sessions.end()) {
      LOG(FATAL) << "session_iter:AuthSessionManager expired a session it is "
                    "not managing";
    }
    if (session_iter->second == nullptr) {
      user_iter->second.zombie_session = iter->second;
    }
    user_iter->second.auth_sessions.erase(session_iter);
    token_to_user_.erase(token_iter);
    if (!user_iter->second.zombie_session.has_value() &&
        user_iter->second.auth_sessions.empty()) {
      user_auth_sessions_.erase(user_iter);
    }
    ++iter;
    ++expired_sessions;
  }
  // Erase all of the entries from the map that were just removed.
  iter = auth_session_expiring_soon_map_.erase(
      auth_session_expiring_soon_map_.begin(), iter);
  if (expired_sessions > 0) {
    LOG(INFO) << "AuthSession: " << expired_sessions << " AuthSession expired.";
  }
  // Reset the expiration timer to run again based on what's left in the map.
  ResetExpirationTimer();
}

void AuthSessionManager::MarkNotInUse(std::unique_ptr<AuthSession> session) {
  // Find the session map for this session's user. If no such map exists then
  // this session has been removed and there are no sessions (or work) left for
  // this user. Just return and let |session| be destroyed.
  auto user_iter = user_auth_sessions_.find(session->obfuscated_username());
  if (user_iter == user_auth_sessions_.end()) {
    return;
  }
  // The user is still active. Return this session to the session map. If its
  // entry no longer exists then the session has been removed and we can destroy
  // |session|, but we still need to kick off any pending work the user has.
  auto& session_map = user_iter->second.auth_sessions;
  auto session_iter = session_map.find(session->token());
  if (session_iter == session_map.end()) {
    CHECK_EQ(*user_iter->second.zombie_session, session->token());
    user_iter->second.zombie_session = std::nullopt;
    session = nullptr;
  } else {
    session_iter->second = std::move(session);
  }
  // Run the next item in the work queue. Note that if the next element was
  // scheduled against a session that no longer exists, we need to keep going
  // until we find work that can actually run (or until the queue is empty).
  auto& work_queue = user_iter->second.work_queue;
  while (!work_queue.empty()) {
    PendingWork work = std::move(work_queue.front());
    work_queue.pop();
    session_iter = session_map.find(work.session_token());
    if (session_iter != session_map.end()) {
      std::move(work).Run(
          InUseAuthSession(*this, std::move(session_iter->second)));
      return;
    }
  }
}

AuthSessionManager::PendingWork::PendingWork(
    base::UnguessableToken session_token,
    const base::Location& from_here,
    Callback work_callback,
    base::SequencedTaskRunner* task_runner)
    : session_token_(std::move(session_token)),
      work_callback_(std::move(work_callback)),
      task_runner_(task_runner) {}

AuthSessionManager::PendingWork::PendingWork(PendingWork&& other)
    : session_token_(std::move(other.session_token_)),
      from_here_(std::move(other.from_here_)),
      work_callback_(std::move(other.work_callback_)),
      task_runner_(other.task_runner_) {
  other.work_callback_ = std::nullopt;
}

AuthSessionManager::PendingWork& AuthSessionManager::PendingWork::operator=(
    PendingWork&& other) {
  session_token_ = std::move(other.session_token_);
  from_here_ = std::move(other.from_here_);
  work_callback_ = std::move(other.work_callback_);
  task_runner_ = other.task_runner_;
  other.work_callback_ = std::nullopt;
  return *this;
}

AuthSessionManager::PendingWork::~PendingWork() {
  if (work_callback_) {
    std::move(*work_callback_).Run(InUseAuthSession());
  }
}

void AuthSessionManager::PendingWork::Run(InUseAuthSession session) && {
  if (!work_callback_) {
    LOG(FATAL) << "Attempting to run work multiple times";
  }
  Callback work = std::move(*work_callback_);
  work_callback_ = std::nullopt;
  task_runner_->PostTask(
      from_here_,
      base::BindOnce(std::move(work), std::move(session).BindForCallback()));
}

InUseAuthSession::InUseAuthSession() : manager_(nullptr), session_(nullptr) {}

InUseAuthSession::InUseAuthSession(AuthSessionManager& manager,
                                   std::unique_ptr<AuthSession> session)
    : manager_(&manager), session_(std::move(session)) {}

InUseAuthSession::InUseAuthSession(InUseAuthSession&& auth_session)
    : manager_(auth_session.manager_),
      session_(std::move(auth_session.session_)) {}

InUseAuthSession& InUseAuthSession::operator=(InUseAuthSession&& auth_session) {
  if (session_ && manager_) {
    manager_->MarkNotInUse(std::move(session_));
  }
  manager_ = auth_session.manager_;
  session_ = std::move(auth_session.session_);
  return *this;
}

InUseAuthSession::~InUseAuthSession() {
  if (session_ && manager_) {
    manager_->MarkNotInUse(std::move(session_));
  }
}

CryptohomeStatus InUseAuthSession::AuthSessionStatus() const {
  if (session_ && manager_) {
    return OkStatus<CryptohomeError>();
  }
  return MakeStatus<CryptohomeError>(
      CRYPTOHOME_ERR_LOC(kLocAuthSessionManagerAuthSessionNotFound),
      ErrorActionSet({PossibleAction::kReboot}),
      user_data_auth::CryptohomeErrorCode::
          CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

base::TimeDelta InUseAuthSession::GetRemainingTime() const {
  // Find the expiration time of the session. If it doesn't have one then its
  // expiration is pending the object no longer being in use, which we report
  // as zero remaining time.
  std::optional<base::Time> expiration_time;
  for (const auto& [time, token] : manager_->expiration_map_) {
    if (token == session_->token()) {
      expiration_time = time;
      break;
    }
  }

  if (!expiration_time) {
    for (const auto& [time, token] :
         manager_->auth_session_expiring_soon_map_) {
      if (token == session_->token()) {
        expiration_time = time;
        break;
      }
    }
  }

  if (!expiration_time) {
    return base::TimeDelta();
  }
  // If the expiration time is the end of time, then report the max duration.
  if (expiration_time->is_max()) {
    return base::TimeDelta::Max();
  }
  // Given the (finite) expiration time we now have, compute the remaining
  // time. If the expiration time is in the past (e.g. because the expiration
  // timer hasn't fired yet) then we clamp the time to zero.
  base::TimeDelta time_left = *expiration_time - manager_->clock_->Now();
  return time_left.is_negative() ? base::TimeDelta() : time_left;
}

CryptohomeStatus InUseAuthSession::ExtendTimeout(base::TimeDelta extension) {
  // Find the existing expiration time of the session. If it doesn't have one
  // then we check the expring soon map.

  auto iter = manager_->expiration_map_.begin();
  while (iter != manager_->expiration_map_.end() &&
         iter->second != session_->token()) {
    ++iter;
  }
  if (iter == manager_->expiration_map_.end()) {
    return ExtendExpiringSoonTimeout(extension);
  }
  // Remove the existing expiration entry and add a new one with the new time.
  base::Time new_time =
      std::max(iter->first, manager_->clock_->Now() + extension);
  manager_->expiration_map_.erase(iter);
  manager_->expiration_map_.emplace(new_time, session_->token());
  manager_->ResetExpirationTimer();
  return OkStatus<CryptohomeError>();
}

std::unique_ptr<BoundAuthSession> InUseAuthSession::BindForCallback() && {
  return std::make_unique<BoundAuthSession>(std::move(*this));
}

void InUseAuthSession::Release() && {
  *this = InUseAuthSession();
}

BoundAuthSession::BoundAuthSession(InUseAuthSession auth_session)
    : session_(std::move(auth_session)) {
  // Setup the initial timeout, unless the session this is bound to is already
  // invalid and so releasing it would be redundant.
  if (session_.AuthSessionStatus().ok()) {
    ScheduleReleaseCheck(kTimeout);
  }
}

InUseAuthSession BoundAuthSession::Take() && {
  timeout_timer_.Stop();
  return std::move(session_);
}

void BoundAuthSession::ReleaseSessionIfBlocking() {
  // If the session is already gone, nothing to do.
  if (!session_.AuthSessionStatus().ok()) {
    return;
  }
  // If the session is blocking any pending work, release it.
  const auto& session_map = session_.manager_->user_auth_sessions_;
  auto sessions_iter = session_map.find(session_->obfuscated_username());
  if (sessions_iter != session_map.end() &&
      !sessions_iter->second.work_queue.empty()) {
    LOG(WARNING)
        << "Timeout on bound auth session, releasing back to session manager";
    session_->CancelAllOutstandingAsyncCallbacks();
    session_ = InUseAuthSession();
    return;
  }
  // If we get here the session is still live but isn't blocking anything so
  // reset the timer to check again.
  ScheduleReleaseCheck(kShortTimeout);
}

void BoundAuthSession::ScheduleReleaseCheck(base::TimeDelta delay) {
  // It's safe to use Unretained here because if |this| is destroyed then the
  // timer will be destroyed and the callback cancelled.
  timeout_timer_.Start(
      FROM_HERE, session_.manager_->clock_->Now() + delay,
      base::BindOnce(&BoundAuthSession::ReleaseSessionIfBlocking,
                     base::Unretained(this)));
}

CryptohomeStatus InUseAuthSession::ExtendExpiringSoonTimeout(
    base::TimeDelta extension) {
  // Find the existing expiration time of the session. If it doesn't have one
  // then the session has already been expired pending the session no longer
  // being in use. This cannot be reverted and so the extend fails.
  auto iter = manager_->auth_session_expiring_soon_map_.begin();
  while (iter != manager_->auth_session_expiring_soon_map_.end() &&
         iter->second != session_->token()) {
    ++iter;
  }
  if (iter == manager_->auth_session_expiring_soon_map_.end()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthSessionTimedOutInExtend),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kRetry,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  }
  // Remove the existing expiration entry and add a new one with the new time.
  base::Time new_time =
      std::max(iter->first, manager_->clock_->Now() + extension);
  manager_->auth_session_expiring_soon_map_.erase(iter);
  manager_->expiration_map_.emplace(new_time, session_->token());
  manager_->ResetExpirationTimer();
  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome

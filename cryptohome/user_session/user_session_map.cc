// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session/user_session_map.h"

#include <string>
#include <utility>
#include <variant>

#include <base/check.h>
#include <base/memory/scoped_refptr.h>

namespace cryptohome {
namespace {

// Helper template for doing visit stuff.
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
// Explicit deduction guide. Can be removed once C++20 is supported.
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// Helper function to initialize the verifier forwarder internal variant.
std::variant<UserSession*,
             std::map<std::string, std::unique_ptr<CredentialVerifier>>>
MakeForwarderVariant(const std::string& account_id,
                     UserSessionMap* user_session_map) {
  if (UserSession* user_session = user_session_map->Find(account_id)) {
    return user_session;
  } else {
    return std::map<std::string, std::unique_ptr<CredentialVerifier>>();
  }
}

}  // namespace

UserSessionMap::VerifierForwarder::VerifierForwarder(
    std::string account_id, UserSessionMap* user_session_map)
    : account_id_(std::move(account_id)),
      user_session_map_(user_session_map),
      forwarding_destination_(
          MakeForwarderVariant(account_id_, user_session_map_)) {
  user_session_map_->verifier_forwarders_[account_id_] = this;
}

UserSessionMap::VerifierForwarder::~VerifierForwarder() {
  user_session_map_->verifier_forwarders_.erase(account_id_);
}

bool UserSessionMap::VerifierForwarder::HasVerifier(const std::string& label) {
  return std::visit(overloaded{[&](UserSession* session) {
                                 return session->HasCredentialVerifier(label);
                               },
                               [&](VerifierMap& verifier_map) {
                                 return verifier_map.find(label) !=
                                        verifier_map.end();
                               }},
                    forwarding_destination_);
}

void UserSessionMap::VerifierForwarder::AddVerifier(
    std::unique_ptr<CredentialVerifier> verifier) {
  const std::string& label = verifier->auth_factor_label();
  std::visit(overloaded{[&](UserSession* session) {
                          session->AddCredentialVerifier(std::move(verifier));
                        },
                        [&](VerifierMap& verifier_map) {
                          verifier_map[label] = std::move(verifier);
                        }},
             forwarding_destination_);
}

void UserSessionMap::VerifierForwarder::RemoveVerifier(
    const std::string& label) {
  std::visit(
      overloaded{[&](UserSession* session) {
                   session->RemoveCredentialVerifierForKeyLabel(label);
                 },
                 [&](VerifierMap& verifier_map) { verifier_map.erase(label); }},
      forwarding_destination_);
}

void UserSessionMap::VerifierForwarder::Resolve(UserSession* session) {
  // Move any existing verifiers into the session.
  if (VerifierMap* vmap = std::get_if<VerifierMap>(&forwarding_destination_)) {
    for (auto& [label, verifier] : *vmap) {
      session->AddCredentialVerifier(std::move(verifier));
    }
  }
  // Attach the session to the forwarder, which will also clear the map.
  forwarding_destination_ = session;
}

void UserSessionMap::VerifierForwarder::Detach() {
  // Change the forwarding destination to a new map for capturing verifiers.
  forwarding_destination_ = VerifierMap();
}

bool UserSessionMap::Add(const std::string& account_id,
                         std::unique_ptr<UserSession> session) {
  DCHECK(session);
  auto [storage_iter, was_inserted] =
      storage_.insert({account_id, std::move(session)});
  auto forwarder_iter = verifier_forwarders_.find(account_id);
  if (forwarder_iter != verifier_forwarders_.end()) {
    forwarder_iter->second->Resolve(storage_iter->second.get());
  }
  return was_inserted;
}

bool UserSessionMap::Remove(const std::string& account_id) {
  auto forwarder_iter = verifier_forwarders_.find(account_id);
  if (forwarder_iter != verifier_forwarders_.end()) {
    forwarder_iter->second->Detach();
  }
  return storage_.erase(account_id) != 0;
}

UserSession* UserSessionMap::Find(const std::string& account_id) {
  auto iter = storage_.find(account_id);
  if (iter == storage_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

const UserSession* UserSessionMap::Find(const std::string& account_id) const {
  auto iter = storage_.find(account_id);
  if (iter == storage_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

}  // namespace cryptohome

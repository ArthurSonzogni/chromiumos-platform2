// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session/user_session.h"

#include <string>
#include <utility>
#include <vector>

#include "cryptohome/credential_verifier.h"

namespace cryptohome {

void UserSession::AddCredentialVerifier(
    std::unique_ptr<CredentialVerifier> verifier) {
  if (verifier->auth_factor_label().empty()) {
    AuthFactorType type = verifier->auth_factor_type();
    type_to_credential_verifier_[type] = std::move(verifier);
  } else {
    std::string label = verifier->auth_factor_label();
    label_to_credential_verifier_[std::move(label)] = std::move(verifier);
  }
}

bool UserSession::HasCredentialVerifier() const {
  return !label_to_credential_verifier_.empty() ||
         !type_to_credential_verifier_.empty();
}

bool UserSession::HasCredentialVerifier(const std::string& label) const {
  return label_to_credential_verifier_.find(label) !=
         label_to_credential_verifier_.end();
}

bool UserSession::HasCredentialVerifier(AuthFactorType type) const {
  return type_to_credential_verifier_.find(type) !=
         type_to_credential_verifier_.end();
}

const CredentialVerifier* UserSession::FindCredentialVerifier(
    const std::string& label) const {
  auto iter = label_to_credential_verifier_.find(label);
  if (iter != label_to_credential_verifier_.end()) {
    return iter->second.get();
  }
  return nullptr;
}

const CredentialVerifier* UserSession::FindCredentialVerifier(
    AuthFactorType type) const {
  auto iter = type_to_credential_verifier_.find(type);
  if (iter != type_to_credential_verifier_.end()) {
    return iter->second.get();
  }
  return nullptr;
}

std::vector<const CredentialVerifier*> UserSession::GetCredentialVerifiers()
    const {
  std::vector<const CredentialVerifier*> verifiers;
  verifiers.reserve(label_to_credential_verifier_.size() +
                    type_to_credential_verifier_.size());
  for (const auto& [unused, verifier] : label_to_credential_verifier_) {
    verifiers.push_back(verifier.get());
  }
  for (const auto& [unused, verifier] : type_to_credential_verifier_) {
    verifiers.push_back(verifier.get());
  }
  return verifiers;
}

std::unique_ptr<CredentialVerifier> UserSession::ReleaseCredentialVerifier(
    const std::string& key_label) {
  auto iter = label_to_credential_verifier_.find(key_label);
  if (iter != label_to_credential_verifier_.end()) {
    auto verifier = std::move(iter->second);
    label_to_credential_verifier_.erase(iter);
    return verifier;
  }
  return nullptr;
}

std::unique_ptr<CredentialVerifier> UserSession::ReleaseCredentialVerifier(
    AuthFactorType type) {
  auto iter = type_to_credential_verifier_.find(type);
  if (iter != type_to_credential_verifier_.end()) {
    auto verifier = std::move(iter->second);
    type_to_credential_verifier_.erase(iter);
    return verifier;
  }
  return nullptr;
}

}  // namespace cryptohome

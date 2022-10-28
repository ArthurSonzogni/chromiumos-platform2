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
  const std::string& label = verifier->auth_factor_label();
  label_to_credential_verifier_[label] = std::move(verifier);
}

bool UserSession::HasCredentialVerifier() const {
  return !label_to_credential_verifier_.empty();
}

bool UserSession::HasCredentialVerifier(const std::string& label) const {
  return label_to_credential_verifier_.find(label) !=
         label_to_credential_verifier_.end();
}

const CredentialVerifier* UserSession::FindCredentialVerifier(
    const std::string& label) const {
  auto iter = label_to_credential_verifier_.find(label);
  if (iter != label_to_credential_verifier_.end()) {
    return iter->second.get();
  }
  return nullptr;
}

std::vector<const CredentialVerifier*> UserSession::GetCredentialVerifiers()
    const {
  std::vector<const CredentialVerifier*> verifiers;
  verifiers.reserve(label_to_credential_verifier_.size());
  for (const auto& [unused, verifier] : label_to_credential_verifier_) {
    verifiers.push_back(verifier.get());
  }
  return verifiers;
}

void UserSession::RemoveCredentialVerifierForKeyLabel(
    const std::string& key_label) {
  // Remove the matching credential verifier, if it exists.
  label_to_credential_verifier_.erase(key_label);

  // Also clear the KeyData, if it matches the given label.
  if (key_data_.label() == key_label) {
    key_data_.Clear();
  }
}

}  // namespace cryptohome

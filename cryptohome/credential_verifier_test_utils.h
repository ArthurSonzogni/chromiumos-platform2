// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIAL_VERIFIER_TEST_UTILS_H_
#define CRYPTOHOME_CREDENTIAL_VERIFIER_TEST_UTILS_H_

#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/credential_verifier.h"

namespace cryptohome {

// Matcher for verifying that a CredentialVerifier* has the specified label and
// matches against the specified password.
class IsVerifierPtrWithLabelAndPasswordMatcher
    : public ::testing::MatcherInterface<const CredentialVerifier*> {
 public:
  IsVerifierPtrWithLabelAndPasswordMatcher(std::string label,
                                           std::string password)
      : label_(std::move(label)), password_(std::move(password)) {}

  bool MatchAndExplain(
      const CredentialVerifier* verifier,
      ::testing::MatchResultListener* listener) const override {
    if (!verifier) {
      *listener << "verifier is null";
      return false;
    }

    bool matches = true;
    if (verifier->auth_factor_label() != label_) {
      matches = false;
      *listener << "label is: " << verifier->auth_factor_label() << "\n";
    }
    if (!verifier->Verify(brillo::SecureBlob(password_))) {
      matches = false;
      *listener << "expected password does not verify\n";
    }
    return matches;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "has label " << label_ << " and accepts password " << password_;
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not have label " << label_ << " or does not accept password "
        << password_;
  }

 private:
  std::string label_;
  std::string password_;
};
::testing::Matcher<const CredentialVerifier*> IsVerifierPtrWithLabelAndPassword(
    std::string label, std::string password) {
  return ::testing::MakeMatcher<const CredentialVerifier*>(
      new IsVerifierPtrWithLabelAndPasswordMatcher(std::move(label),
                                                   std::move(password)));
}

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIAL_VERIFIER_TEST_UTILS_H_

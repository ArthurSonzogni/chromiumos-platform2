// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_INTENT_H_
#define CRYPTOHOME_AUTH_INTENT_H_

#include <array>

namespace cryptohome {

// An intent specifies the set of operations that can be performed after
// successfully authenticating an Auth Session.
enum class AuthIntent {
  // Intent to decrypt the user's file system keys. Authorizing for this intent
  // allows all privileged operations, e.g., preparing user's vault,
  // adding/updating/removing factors.
  kDecrypt,
  // Intent to simply check whether the authentication succeeds. Authorizing for
  // this intent doesn't allow any privileged operation.
  kVerifyOnly,
  // Intent to unlock the WebAuthn capability. Authorizing for this intent
  // allows the WebAuthn operation.
  kWebAuthn,
};

// All intents as an array. Useful for things like iterating through every
// possible intent type.
inline constexpr AuthIntent kAllAuthIntents[] = {
    AuthIntent::kDecrypt,
    AuthIntent::kVerifyOnly,
    AuthIntent::kWebAuthn,
};

// A template that accepts a list of intents as a parameter pack and then
// exposes them as a static std::array. Normally not necessary but useful in
// certain rare situations where you need to pass an list of intents as a
// template parameter.
template <AuthIntent... kIntents>
struct AuthIntentSequence {
  static constexpr std::array<AuthIntent, sizeof...(kIntents)> kArray = {
      kIntents...};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_INTENT_H_

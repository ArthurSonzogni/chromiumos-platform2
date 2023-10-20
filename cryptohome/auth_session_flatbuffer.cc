// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session_flatbuffer.h"

#include "cryptohome/auth_intent.h"
#include "cryptohome/flatbuffer_schemas/enumerations.h"

namespace cryptohome {

SerializedAuthIntent SerializeAuthIntent(AuthIntent intent) {
  switch (intent) {
    case AuthIntent::kDecrypt:
      return SerializedAuthIntent::kDecrypt;
    case AuthIntent::kVerifyOnly:
      return SerializedAuthIntent::kVerifyOnly;
    case AuthIntent::kWebAuthn:
      return SerializedAuthIntent::kWebAuthn;
  }
}

AuthIntent DeserializeAuthIntent(SerializedAuthIntent intent) {
  switch (intent) {
    case SerializedAuthIntent::kDecrypt:
      return AuthIntent::kDecrypt;
    case SerializedAuthIntent::kVerifyOnly:
      return AuthIntent::kVerifyOnly;
    case SerializedAuthIntent::kWebAuthn:
      return AuthIntent::kWebAuthn;
  }
}

}  // namespace cryptohome

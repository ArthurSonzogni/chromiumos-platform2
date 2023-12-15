// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNALLING_H_
#define CRYPTOHOME_SIGNALLING_H_

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

namespace cryptohome {

// Defines a standard interface for sending D-Bus signals.
class SignallingInterface {
 public:
  SignallingInterface() = default;
  virtual ~SignallingInterface() = default;

  // Send the given signal. All of these functions work the same way: calling
  // "SendXxx" will send the UserDataAuth D-Bus signal named "Xxx".
  virtual void SendAuthenticateAuthFactorCompleted(
      const user_data_auth::AuthenticateAuthFactorCompleted& signal) = 0;
};

// Null implementation of the signalling interface that considers every signal
// to be a no-op. Useful as a default handler in cases where the real D-Bus
// version is not yet available.
class NullSignalling : public SignallingInterface {
 public:
  NullSignalling() = default;

  NullSignalling(const NullSignalling&) = delete;
  NullSignalling& operator=(const NullSignalling&) = delete;

 private:
  void SendAuthenticateAuthFactorCompleted(
      const user_data_auth::AuthenticateAuthFactorCompleted& signal) override {}
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNALLING_H_

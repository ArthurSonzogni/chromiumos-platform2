// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PKCS11_FAKE_PKCS11_TOKEN_H_
#define CRYPTOHOME_PKCS11_FAKE_PKCS11_TOKEN_H_

#include "cryptohome/pkcs11/pkcs11_token.h"

namespace cryptohome {

class FakePkcs11Token final : public Pkcs11Token {
 public:
  FakePkcs11Token() : has_key_(true), ready_(false), restoring_(false) {}

  ~FakePkcs11Token() override = default;

  bool Insert() override {
    has_key_ = false;
    ready_ = true;
    restoring_ = false;
    return true;
  }

  void Remove() override { ready_ = false; }

  bool IsReady() const override { return ready_; }

  void TryRestoring() override {
    if (has_key_) {
      Insert();
      return;
    }

    ready_ = false;
    restoring_ = true;
  }

  bool NeedRestore() const override { return restoring_; }

  void RestoreAuthData(const brillo::SecureBlob& auth_data) override {
    ready_ = true;
    restoring_ = false;
  }

 private:
  bool has_key_;
  bool ready_;
  bool restoring_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PKCS11_FAKE_PKCS11_TOKEN_H_

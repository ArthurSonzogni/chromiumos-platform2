// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_PASSWORD_AUTHORIZATION_DELEGATE_H_
#define TRUNKS_PASSWORD_AUTHORIZATION_DELEGATE_H_

#include <string>

#include <base/gtest_prod_util.h>
#include <chromeos/chromeos_export.h>

#include "trunks/authorization_delegate.h"
#include "trunks/tpm_generated.h"

namespace trunks {

// PasswdAuthorizationDelegate is an implementation of the AuthorizationDelegate
// interface. This delegate is used for password based authorization. Upon
// initialization of this delegate, we feed in the plaintext password. This
// password is then used to authorize the commands issued with this delegate.
// This delegate performs no parameter encryption.
class CHROMEOS_EXPORT PasswordAuthorizationDelegate
    : public AuthorizationDelegate {
 public:
  explicit PasswordAuthorizationDelegate(const std::string& password);
  ~PasswordAuthorizationDelegate() override;
  // AuthorizationDelegate methods.
  bool GetCommandAuthorization(const std::string& command_hash,
                               bool is_command_parameter_encryption_possible,
                               bool is_response_parameter_encryption_possible,
                               std::string* authorization) override;
  bool CheckResponseAuthorization(const std::string& response_hash,
                                  const std::string& authorization) override;
  bool EncryptCommandParameter(std::string* parameter) override;
  bool DecryptResponseParameter(std::string* parameter) override;

 protected:
  FRIEND_TEST(PasswordAuthorizationDelegateTest, NullInitialization);

 private:
  TPM2B_AUTH password_;

  DISALLOW_COPY_AND_ASSIGN(PasswordAuthorizationDelegate);
};

}  // namespace trunks

#endif  // TRUNKS_PASSWORD_AUTHORIZATION_DELEGATE_H_

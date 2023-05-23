// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_MULTIPLE_AUTHORIZATION_DELEGATE_H_
#define TRUNKS_MULTIPLE_AUTHORIZATION_DELEGATE_H_

#include <string>
#include <vector>

#include <base/gtest_prod_util.h>

#include "trunks/authorization_delegate.h"
#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// An authorization delegate to manage multiple authorization sessions for a
// single command.
class TRUNKS_EXPORT MultipleAuthorizations : public AuthorizationDelegate {
 public:
  MultipleAuthorizations() = default;
  ~MultipleAuthorizations() override = default;

  // AuthorizationDelegate methods.
  bool GetCommandAuthorization(const std::string& command_hash,
                               bool is_command_parameter_encryption_possible,
                               bool is_response_parameter_encryption_possible,
                               std::string* authorization) override;
  bool CheckResponseAuthorization(const std::string& response_hash,
                                  const std::string& authorization) override;
  bool EncryptCommandParameter(std::string* parameter) override;
  bool DecryptResponseParameter(std::string* parameter) override;
  bool GetTpmNonce(std::string* nonce) override;

  // Adds an authrization delegate.
  void AddAuthorizationDelegate(AuthorizationDelegate* delegate);

 private:
  std::string ExtractSingleAuthorizationResponse(std::string* all_responses);

  std::vector<AuthorizationDelegate*> delegates_;
};

}  // namespace trunks

#endif  // TRUNKS_MULTIPLE_AUTHORIZATION_DELEGATE_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/multiple_authorization_delegate.h"

#include <base/logging.h>

#include "trunks/authorization_delegate.h"
#include "trunks/tpm_generated.h"

namespace trunks {

void MultipleAuthorizations::AddAuthorizationDelegate(
    AuthorizationDelegate* delegate) {
  delegates_.push_back(delegate);
}

bool MultipleAuthorizations::GetCommandAuthorization(
    const std::string& command_hash,
    bool is_command_parameter_encryption_possible,
    bool is_response_parameter_encryption_possible,
    std::string* authorization) {
  std::string combined_authorization;
  for (auto delegate : delegates_) {
    std::string authorization;
    if (!delegate->GetCommandAuthorization(
            command_hash, is_command_parameter_encryption_possible,
            is_response_parameter_encryption_possible, &authorization)) {
      return false;
    }
    combined_authorization += authorization;
  }
  *authorization = combined_authorization;
  return true;
}

bool MultipleAuthorizations::CheckResponseAuthorization(
    const std::string& response_hash, const std::string& authorization) {
  std::string mutable_authorization = authorization;
  for (auto delegate : delegates_) {
    if (!delegate->CheckResponseAuthorization(
            response_hash,
            ExtractSingleAuthorizationResponse(&mutable_authorization))) {
      return false;
    }
  }
  return true;
}

bool MultipleAuthorizations::EncryptCommandParameter(std::string* parameter) {
  for (auto delegate : delegates_) {
    if (!delegate->EncryptCommandParameter(parameter)) {
      return false;
    }
  }
  return true;
}

bool MultipleAuthorizations::DecryptResponseParameter(std::string* parameter) {
  for (auto delegate : delegates_) {
    if (!delegate->DecryptResponseParameter(parameter)) {
      return false;
    }
  }
  return true;
}

bool MultipleAuthorizations::GetTpmNonce(std::string* nonce) {
  return false;
}

std::string MultipleAuthorizations::ExtractSingleAuthorizationResponse(
    std::string* all_responses) {
  std::string response;
  trunks::TPMS_AUTH_RESPONSE not_used;
  if (TPM_RC_SUCCESS !=
      Parse_TPMS_AUTH_RESPONSE(all_responses, &not_used, &response)) {
    return std::string();
  }
  return response;
}

}  // namespace trunks

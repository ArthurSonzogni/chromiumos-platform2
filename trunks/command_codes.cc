// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/command_codes.h"

#include <string>

#include <base/check_op.h>
#include <base/notreached.h>

namespace trunks {

std::string CreateCommand(TPM_CC command_code) {
  // 2 bytes TPMI_ST_COMMAND_TAG + 4 bytes command size + 4 bytes command code.
  constexpr uint32_t kCommandSize = 10;
  std::string command;
  CHECK_EQ(Serialize_TPM_ST(TPM_ST_NO_SESSIONS, &command), TPM_RC_SUCCESS);
  CHECK_EQ(Serialize_UINT32(kCommandSize, &command), TPM_RC_SUCCESS);
  CHECK_EQ(Serialize_TPM_CC(command_code, &command), TPM_RC_SUCCESS);
  return command;
}

TPM_RC GetCommandCode(const std::string& command, TPM_CC& cc) {
  std::string buffer(command);
  TPM_ST tag;
  TPM_RC parse_rc = Parse_TPM_ST(&buffer, &tag, nullptr);
  if (parse_rc != TPM_RC_SUCCESS) {
    return parse_rc;
  }
  UINT32 response_size;
  parse_rc = Parse_UINT32(&buffer, &response_size, nullptr);
  if (parse_rc != TPM_RC_SUCCESS) {
    return parse_rc;
  }
  if (response_size != command.size()) {
    return TPM_RC_SIZE;
  }
  parse_rc = Parse_TPM_CC(&buffer, &cc, nullptr);
  if (parse_rc != TPM_RC_SUCCESS) {
    return parse_rc;
  }
  return TPM_RC_SUCCESS;
}

bool IsGenericTpmCommand(TPM_CC command_code) {
  return TPM_CC_FIRST <= command_code && command_code <= TPM_CC_LAST;
}

}  // namespace trunks

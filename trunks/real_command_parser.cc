// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/real_command_parser.h"

#include <string>

#include <base/logging.h>

#include "trunks/tpm_generated.h"

namespace trunks {

TPM_RC RealCommandParser::ParseHeader(std::string* command,
                                      TPMI_ST_COMMAND_TAG* tag,
                                      UINT32* size,
                                      TPM_CC* cc) {
  const UINT32 command_size = command->size();
  TPM_RC rc = Parse_TPMI_ST_COMMAND_TAG(command, tag, nullptr);
  if (rc) {
    return rc;
  }
  if (*tag != TPM_ST_SESSIONS && *tag != TPM_ST_NO_SESSIONS) {
    return TPM_RC_BAD_TAG;
  }
  rc = Parse_UINT32(command, size, nullptr);
  if (rc) {
    return rc;
  }
  if (command_size != *size) {
    return TPM_RC_COMMAND_SIZE;
  }
  return Parse_TPM_CC(command, cc, nullptr);
}

TPM_RC RealCommandParser::ParseCommandGetCapability(std::string* command,
                                                    TPM_CAP* cap,
                                                    UINT32* property,
                                                    UINT32* property_count) {
  TPMI_ST_COMMAND_TAG tag;
  UINT32 size;
  TPM_CC cc;
  TPM_RC rc = ParseHeader(command, &tag, &size, &cc);
  if (rc) {
    return rc;
  }

  if (cc != TPM_CC_GetCapability) {
    LOG(DFATAL) << __func__
                << ": Expecting command code: " << TPM_CC_GetCapability
                << "; got " << cc;
    return TPM_RC_COMMAND_CODE;
  }

  rc = Parse_TPM_CAP(command, cap, nullptr);
  if (rc) {
    return rc;
  }

  // Note that validation of `cap` is not implemented in this parser because we
  // don't have the usecase.

  rc = Parse_UINT32(command, property, nullptr);
  if (rc) {
    return rc;
  }
  rc = Parse_UINT32(command, property_count, nullptr);
  if (rc) {
    return rc;
  }

  if (!command->empty()) {
    rc = TPM_RC_SIZE;
  }
  return rc;
}

}  // namespace trunks

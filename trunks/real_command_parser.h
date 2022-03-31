// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_REAL_COMMAND_PARSER_H_
#define TRUNKS_REAL_COMMAND_PARSER_H_

#include "trunks/command_parser.h"

#include <string>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// An implementation that parses the real (i.e., defined by TPM2.0 spec) TPM
// request.
class TRUNKS_EXPORT RealCommandParser : public CommandParser {
 public:
  ~RealCommandParser() override = default;

  TPM_RC ParseHeader(std::string* command,
                     TPMI_ST_COMMAND_TAG* tag,
                     UINT32* command_size,
                     TPM_CC* cc) override;

  // Parses a real `TPM2_GetCapability` command.
  // Note that `command` is supposed to have `TPM_CC_GetCapability`. Otherwise,
  // it crashes in debug mode (and return error in release).
  TPM_RC ParseCommandGetCapability(std::string* command,
                                   TPM_CAP* cap,
                                   UINT32* property,
                                   UINT32* property_count) override;
};

}  // namespace trunks

#endif  // TRUNKS_REAL_COMMAND_PARSER_H_

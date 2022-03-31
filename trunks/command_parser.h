// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_COMMAND_PARSER_H_
#define TRUNKS_COMMAND_PARSER_H_

#include <string>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// TPM message header size.
inline constexpr size_t kHeaderSize = 10;

// A class that parses TPM commands.
class TRUNKS_EXPORT CommandParser {
 public:
  virtual ~CommandParser() = default;

  // Parses TPM command headers, including the session tag, the request size,
  // and the command code.
  virtual TPM_RC ParseHeader(std::string* command,
                             TPMI_ST_COMMAND_TAG* tag,
                             UINT32* command_size,
                             TPM_CC* cc) = 0;
};

}  // namespace trunks

#endif  // TRUNKS_COMMAND_PARSER_H_

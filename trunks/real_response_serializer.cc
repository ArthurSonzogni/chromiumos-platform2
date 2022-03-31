// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/real_response_serializer.h"

#include <string>

#include "trunks/command_parser.h"
#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

void RealResponseSerializer::SerializeHeaderOnlyResponse(
    TPM_RC rc, std::string* response) {
  const TPMI_ST_COMMAND_TAG tag =
      (rc == TPM_RC_BAD_TAG ? TPM_ST_RSP_COMMAND : TPM_ST_NO_SESSIONS);
  Serialize_TPMI_ST_COMMAND_TAG(tag, response);
  Serialize_UINT32(kHeaderSize, response);
  Serialize_TPM_RC(rc, response);
}

}  // namespace trunks

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/commands/get_capability_command.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/logging.h>
#include <trunks/command_parser.h>
#include <trunks/response_serializer.h>

#include "vtpm/backends/tpm_handle_manager.h"

namespace vtpm {

GetCapabilityCommand::GetCapabilityCommand(
    trunks::CommandParser* command_parser,
    trunks::ResponseSerializer* response_serializer,
    TpmHandleManager* tpm_handle_manager)
    : command_parser_(command_parser),
      response_serializer_(response_serializer),
      tpm_handle_manager_(tpm_handle_manager) {
  CHECK(command_parser_);
  CHECK(response_serializer_);
  CHECK(tpm_handle_manager_);
}

void GetCapabilityCommand::Run(const std::string& command,
                               CommandResponseCallback callback) {
  trunks::TPM_CAP cap;
  trunks::UINT32 property;
  trunks::UINT32 property_count;
  std::string buffer = command;
  trunks::TPM_RC rc = command_parser_->ParseCommandGetCapability(
      &buffer, &cap, &property, &property_count);
  if (rc) {
    ReturnWithError(rc, std::move(callback));
    return;
  }
  // Only "handles" capability is supported. Also, the supported handle types
  // are judged by `tpm_handle_manager_`.
  if (cap != trunks::TPM_CAP_HANDLES) {
    rc = trunks::TPM_RC_VALUE;
  } else if (!tpm_handle_manager_->IsHandleTypeSuppoerted(property)) {
    rc = trunks::TPM_RC_HANDLE;
  }
  if (rc) {
    ReturnWithError(rc, std::move(callback));
    return;
  }

  trunks::TPMS_CAPABILITY_DATA cap_data = {.capability =
                                               trunks::TPM_CAP_HANDLES};
  std::vector<trunks::TPM_HANDLE> found_handles;
  rc = tpm_handle_manager_->GetHandleList(property, &found_handles);
  if (rc) {
    ReturnWithError(rc, std::move(callback));
    return;
  }

  if (property_count > MAX_CAP_HANDLES)
    property_count = MAX_CAP_HANDLES;

  const trunks::TPMI_YES_NO has_more =
      (found_handles.size() > property_count ? YES : NO);
  cap_data.data.handles.count =
      has_more ? property_count : found_handles.size();

  std::copy(found_handles.begin(),
            found_handles.begin() + cap_data.data.handles.count,
            cap_data.data.handles.handle);

  std::string response;
  response_serializer_->SerializeResponseGetCapability(has_more, cap_data,
                                                       &response);
  std::move(callback).Run(response);
  return;
}

void GetCapabilityCommand::ReturnWithError(trunks::TPM_RC rc,
                                           CommandResponseCallback callback) {
  DCHECK_NE(rc, trunks::TPM_RC_SUCCESS);
  std::string response;
  response_serializer_->SerializeHeaderOnlyResponse(rc, &response);
  std::move(callback).Run(response);
}

}  // namespace vtpm

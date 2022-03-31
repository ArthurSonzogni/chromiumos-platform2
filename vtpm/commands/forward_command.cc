// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/commands/forward_command.h"

#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/check.h>
#include <trunks/tpm_generated.h>

#include "vtpm/backends/scoped_host_key_handle.h"

namespace vtpm {

namespace {

// By spec, the interface type we are trying to parse is `TPMI_DH_OBJECT`, which
// `TPM_HANDLE` should be converted to/from.
static_assert(sizeof(trunks::TPMI_DH_OBJECT) == sizeof(trunks::TPM_HANDLE),
              "TPMI_DH_OBJECT should be the same size of TPM_HANDLE by spec");

}  // namespace

ForwardCommand::ForwardCommand(trunks::CommandParser* command_parser,
                               trunks::ResponseSerializer* response_serializer,
                               StaticAnalyzer* static_analyzer,
                               TpmHandleManager* tpm_handle_manager,
                               Command* direct_forwarder)
    : command_parser_(command_parser),
      response_serializer_(response_serializer),
      static_analyzer_(static_analyzer),
      tpm_handle_manager_(tpm_handle_manager),
      direct_forwarder_(direct_forwarder) {
  CHECK(command_parser_);
  CHECK(response_serializer_);
  CHECK(static_analyzer_);
  CHECK(tpm_handle_manager_);
  CHECK(direct_forwarder_);
}

void ForwardCommand::Run(const std::string& command,
                         CommandResponseCallback callback) {
  std::string buffer = command;
  trunks::TPMI_ST_COMMAND_TAG tag;
  trunks::UINT32 size;
  trunks::TPM_CC cc;
  const trunks::TPM_RC rc =
      command_parser_->ParseHeader(&buffer, &tag, &size, &cc);
  if (rc) {
    ReturnWithError(rc, std::move(callback));
    return;
  }

  std::vector<trunks::TPM_HANDLE> handles;
  std::vector<ScopedHostKeyHandle> host_handles;
  const int handle_count = static_analyzer_->GetCommandHandleCount(cc);
  // Creates the buffer w/ exactly the expected handles region, so short data
  // can be detected when unmarshalling.
  buffer = command.substr(trunks::kHeaderSize,
                          handle_count * sizeof(trunks::TPM_HANDLE));
  for (int i = 0; i < handle_count; ++i) {
    trunks::TPM_HANDLE h;
    trunks::TPM_RC rc = trunks::Parse_TPM_HANDLE(&buffer, &h, nullptr);
    if (rc) {
      ReturnWithError(rc, std::move(callback));
      return;
    }
    ScopedHostKeyHandle host_handle;
    rc = tpm_handle_manager_->TranslateHandle(h, &host_handle);
    if (rc) {
      ReturnWithError(rc, std::move(callback));
      return;
    }
    // Stores the handle to retains the ownership.
    host_handles.emplace_back(std::move(host_handle));
  }
  std::string host_handle_bytes;
  for (const auto& h : host_handles) {
    trunks::Serialize_TPM_HANDLE(h.Get(), &host_handle_bytes);
  }
  std::string host_command = command;

  host_command.replace(trunks::kHeaderSize, host_handle_bytes.size(),
                       host_handle_bytes);
  CHECK_EQ(command.size(), host_command.size());
  direct_forwarder_->Run(host_command, std::move(callback));
}

void ForwardCommand::ReturnWithError(trunks::TPM_RC rc,
                                     CommandResponseCallback callback) {
  DCHECK_NE(rc, trunks::TPM_RC_SUCCESS);
  std::string response;
  response_serializer_->SerializeHeaderOnlyResponse(rc, &response);
  std::move(callback).Run(response);
}

}  // namespace vtpm

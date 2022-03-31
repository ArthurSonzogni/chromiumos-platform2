// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/commands/virtualizer.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/callback.h>
#include <trunks/command_parser.h>
#include <trunks/response_serializer.h>
#include <trunks/tpm_generated.h>

#include "vtpm/commands/unsupported_command.h"

namespace vtpm {

std::unique_ptr<Virtualizer> Virtualizer::Create(Virtualizer::Profile profile) {
  std::unique_ptr<Virtualizer> v =
      std::unique_ptr<Virtualizer>(new Virtualizer());
  if (profile == Virtualizer::Profile::kGLinux) {
    v->command_parser_ = &v->real_command_parser_;
    v->response_serializer_ = &v->real_response_serializer_;

    // Use an `UnsupportedCommand` as fallback.
    v->commands_.emplace_back(
        std::make_unique<UnsupportedCommand>(v->response_serializer_));
    v->fallback_command_ = v->commands_.back().get();
    // Others are not implemented yet.
  }
  return v;
}

Virtualizer::Virtualizer(trunks::CommandParser* parser,
                         trunks::ResponseSerializer* serializer,
                         std::unordered_map<trunks::TPM_CC, Command*> table,
                         Command* fallback_command)
    : command_parser_(parser),
      response_serializer_(serializer),
      command_table_(std::move(table)),
      fallback_command_(fallback_command) {}

void Virtualizer::Run(const std::string& command,
                      CommandResponseCallback callback) {
  std::string buffer = command;
  trunks::TPMI_ST_COMMAND_TAG tag;
  trunks::UINT32 size;
  trunks::TPM_CC cc;
  const trunks::TPM_RC rc =
      command_parser_->ParseHeader(&buffer, &tag, &size, &cc);

  if (rc) {
    std::string response;
    response_serializer_->SerializeHeaderOnlyResponse(rc, &response);
    std::move(callback).Run(response);
    return;
  }

  if (command_table_.count(cc) == 0) {
    fallback_command_->Run(command, std::move(callback));
    return;
  }

  command_table_[cc]->Run(command, std::move(callback));
}

}  // namespace vtpm

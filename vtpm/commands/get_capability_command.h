// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_COMMANDS_GET_CAPABILITY_COMMAND_H_
#define VTPM_COMMANDS_GET_CAPABILITY_COMMAND_H_

#include "vtpm/commands/command.h"

#include <string>

#include <base/callback.h>
#include <trunks/command_parser.h>
#include <trunks/response_serializer.h>

#include "vtpm/backends/tpm_handle_manager.h"

namespace vtpm {

class GetCapabilityCommand : public Command {
 public:
  GetCapabilityCommand(trunks::CommandParser* command_parser,
                       trunks::ResponseSerializer* response_serializer,
                       TpmHandleManager* tpm_handle_manager);
  void Run(const std::string& command,
           CommandResponseCallback callback) override;

 private:
  void ReturnWithError(trunks::TPM_RC rc, CommandResponseCallback callback);

  trunks::CommandParser* const command_parser_;
  trunks::ResponseSerializer* const response_serializer_;
  TpmHandleManager* const tpm_handle_manager_;
};

}  // namespace vtpm

#endif  // VTPM_COMMANDS_GET_CAPABILITY_COMMAND_H_

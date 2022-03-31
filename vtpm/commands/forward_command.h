// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_COMMANDS_FORWARD_COMMAND_H_
#define VTPM_COMMANDS_FORWARD_COMMAND_H_

#include "vtpm/commands/command.h"

#include <string>

#include <base/callback.h>
#include <trunks/command_parser.h>
#include <trunks/response_serializer.h>

#include "vtpm/backends/static_analyzer.h"
#include "vtpm/backends/tpm_handle_manager.h"

namespace vtpm {

// This class forwards the TPM command from a guest, after necessary translation
// to the command, to the host TPM.
class ForwardCommand : public Command {
 public:
  ForwardCommand(trunks::CommandParser* command_parser,
                 trunks::ResponseSerializer* response_serializer,
                 StaticAnalyzer* static_analyzer,
                 TpmHandleManager* tpm_handle_manager,
                 Command* direct_forwarder);

  // Forwards `command` to the host TPM. Does any pre-processing or
  // post-processing needed for a command code, if needed.
  void Run(const std::string& command,
           CommandResponseCallback callback) override;

 private:
  void ReturnWithError(trunks::TPM_RC rc, CommandResponseCallback callback);

  trunks::CommandParser* const command_parser_;
  trunks::ResponseSerializer* const response_serializer_;
  StaticAnalyzer* const static_analyzer_;
  TpmHandleManager* const tpm_handle_manager_;
  Command* const direct_forwarder_;
};

}  // namespace vtpm

#endif  // VTPM_COMMANDS_FORWARD_COMMAND_H_

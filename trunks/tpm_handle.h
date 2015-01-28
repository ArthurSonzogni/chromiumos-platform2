// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_HANDLE_H_
#define TRUNKS_TPM_HANDLE_H_

#include "trunks/command_transceiver.h"

#include <string>

#include "trunks/error_codes.h"

namespace trunks {

// Sends commands to a TPM device via a handle to /dev/tpm0. All commands are
// sent synchronously. The SendCommand method is supported but does not return
// until a response is received and the callback has been called.
//
// Example:
//   TpmHandle handle;
//   if (!handle.Init()) {...}
//   std::string response = handle.SendCommandAndWait(command);
class TpmHandle: public CommandTransceiver  {
 public:
  TpmHandle();
  virtual ~TpmHandle();

  // Initializes a TpmHandle instance. This method must be called successfully
  // before any other method. Returns true on success.
  bool Init();

  // CommandTranceiver methods.
  void SendCommand(const std::string& command,
                   const ResponseCallback& callback) override;
  std::string SendCommandAndWait(const std::string& command) override;

 private:
  // Writes a |command| to /dev/tpm0 and reads the |response|. Returns
  // TPM_RC_SUCCESS on success.
  TPM_RC SendCommandInternal(const std::string& command, std::string* response);

  // Sanity checks a |message| for header correctness. Returns TPM_RC_SUCCESS on
  // success.
  TPM_RC VerifyMessage(const std::string& message);

  int fd_;  // A file descriptor for /dev/tpm0.

  DISALLOW_COPY_AND_ASSIGN(TpmHandle);
};

}  // namespace trunks

#endif  // TRUNKS_TPM_HANDLE_H_

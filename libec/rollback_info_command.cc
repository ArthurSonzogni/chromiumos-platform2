// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/rollback_info_command.h"

namespace ec {

uint32_t RollbackInfoCommand::GetVersion() const {
  return command_version_;
}

int32_t RollbackInfoCommand::ID() const {
  return cmd_v0_->ID();
}

int32_t RollbackInfoCommand::MinVersion() const {
  return cmd_v0_->MinVersion();
}

int32_t RollbackInfoCommand::RWVersion() const {
  return cmd_v0_->RWVersion();
}

bool RollbackInfoCommand::Run(int ec_fd) {
  return cmd_v0_->Run(ec_fd);
}

bool RollbackInfoCommand::Run(ec::EcUsbEndpointInterface& uep) {
  return cmd_v0_->Run(uep);
}

bool RollbackInfoCommand::RunWithMultipleAttempts(int fd, int num_attempts) {
  return cmd_v0_->RunWithMultipleAttempts(fd, num_attempts);
}

uint32_t RollbackInfoCommand::Version() const {
  return cmd_v0_->Version();
}

uint32_t RollbackInfoCommand::Command() const {
  return cmd_v0_->Command();
}

uint32_t RollbackInfoCommand::Result() const {
  return cmd_v0_->Result();
}

}  // namespace ec

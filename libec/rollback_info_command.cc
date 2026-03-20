// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/rollback_info_command.h"

namespace ec {

uint32_t RollbackInfoCommand::GetVersion() const {
  return command_version_;
}

int32_t RollbackInfoCommand::ID() const {
  if (GetVersion() == 1) {
    return cmd_v1_->ID();
  }
  return cmd_v0_->ID();
}

int32_t RollbackInfoCommand::MinVersion() const {
  if (GetVersion() == 1) {
    return cmd_v1_->MinVersion();
  }
  return cmd_v0_->MinVersion();
}

int32_t RollbackInfoCommand::RWVersion() const {
  if (GetVersion() == 1) {
    return cmd_v1_->RWVersion();
  }
  return cmd_v0_->RWVersion();
}

std::optional<bool> RollbackInfoCommand::IsSecretInited() const {
  if (GetVersion() == 1) {
    return cmd_v1_->IsSecretInited();
  }
  return std::nullopt;
}

bool RollbackInfoCommand::Run(int ec_fd) {
  if (GetVersion() == 1) {
    return cmd_v1_->Run(ec_fd);
  }
  return cmd_v0_->Run(ec_fd);
}

bool RollbackInfoCommand::Run(ec::EcUsbEndpointInterface& uep) {
  if (GetVersion() == 1) {
    return cmd_v1_->Run(uep);
  }
  return cmd_v0_->Run(uep);
}

bool RollbackInfoCommand::RunWithMultipleAttempts(int fd, int num_attempts) {
  if (GetVersion() == 1) {
    return cmd_v1_->RunWithMultipleAttempts(fd, num_attempts);
  }
  return cmd_v0_->RunWithMultipleAttempts(fd, num_attempts);
}

uint32_t RollbackInfoCommand::Version() const {
  if (GetVersion() == 1) {
    return cmd_v1_->Version();
  }
  return cmd_v0_->Version();
}

uint32_t RollbackInfoCommand::Command() const {
  if (GetVersion() == 1) {
    return cmd_v1_->Command();
  }
  return cmd_v0_->Command();
}

uint32_t RollbackInfoCommand::Result() const {
  if (GetVersion() == 1) {
    return cmd_v1_->Result();
  }
  return cmd_v0_->Result();
}

}  // namespace ec

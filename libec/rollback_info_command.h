// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_ROLLBACK_INFO_COMMAND_H_
#define LIBEC_ROLLBACK_INFO_COMMAND_H_

#include <memory>
#include <utility>

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT RollbackInfoCommand_v0
    : public EcCommand<EmptyParam, struct ec_response_rollback_info> {
 public:
  RollbackInfoCommand_v0() : EcCommand(EC_CMD_ROLLBACK_INFO, 0) {}
  ~RollbackInfoCommand_v0() override = default;

  int32_t ID() const;
  int32_t MinVersion() const;
  int32_t RWVersion() const;
};

class BRILLO_EXPORT RollbackInfoCommand_v1
    : public EcCommand<EmptyParam, struct ec_response_rollback_info_v1> {
 public:
  RollbackInfoCommand_v1() : EcCommand(EC_CMD_ROLLBACK_INFO, 1) {}
  ~RollbackInfoCommand_v1() override = default;

  int32_t ID() const;
  int32_t MinVersion() const;
  int32_t RWVersion() const;
  bool IsSecretInited() const;
};

class BRILLO_EXPORT RollbackInfoCommand : public EcCommandInterface {
 public:
  explicit RollbackInfoCommand(uint32_t version) : command_version_(version) {
    if (version == 1) {
      cmd_v1_ = std::make_unique<RollbackInfoCommand_v1>();
    } else {
      cmd_v0_ = std::make_unique<RollbackInfoCommand_v0>();
    }
  }

  // Only for testing.
  RollbackInfoCommand(uint32_t version,
                      std::unique_ptr<RollbackInfoCommand_v0> v0,
                      std::unique_ptr<RollbackInfoCommand_v1> v1)
      : command_version_(version) {
    if (version == 1) {
      cmd_v1_ = std::move(v1);
    } else {
      cmd_v0_ = std::move(v0);
    }
  }

  bool Run(int ec_fd) override;
  bool Run(ec::EcUsbEndpointInterface& uep) override;
  bool RunWithMultipleAttempts(int fd, int num_attempts) override;
  uint32_t Version() const override;
  uint32_t Command() const override;

  uint32_t Result() const;
  uint32_t GetVersion() const;

  virtual int32_t ID() const;
  virtual int32_t MinVersion() const;
  virtual int32_t RWVersion() const;
  virtual std::optional<bool> IsSecretInited() const;

 private:
  std::unique_ptr<RollbackInfoCommand_v0> cmd_v0_ = nullptr;
  std::unique_ptr<RollbackInfoCommand_v1> cmd_v1_ = nullptr;
  uint32_t command_version_;
};

static_assert(!std::is_copy_constructible<RollbackInfoCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<RollbackInfoCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_ROLLBACK_INFO_COMMAND_H_

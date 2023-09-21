// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_I2C_READ_COMMAND_H_
#define LIBEC_I2C_READ_COMMAND_H_

#include <memory>
#include <type_traits>

#include <base/memory/ptr_util.h>

#include "libec/i2c_passthru_command.h"

namespace ec {

// The command to read data over I2C buses.
class BRILLO_EXPORT I2cReadCommand : public I2cPassthruCommand {
 public:
  // Use factory method instead.
  I2cReadCommand() = default;

  ~I2cReadCommand() override = default;

  // Factory method.
  // @param port I2C port number
  // @param addr8 I2C target address in 8-bit
  // @param offset offset to read from or write to
  // @param read_len number of bytes to read. Should be 1, 2, or 4.
  // @return a pointer to the command or |nullptr| if error
  template <typename T = I2cReadCommand>
  static std::unique_ptr<T> Create(uint8_t port,
                                   uint8_t addr8,
                                   uint8_t offset,
                                   uint8_t read_len) {
    static_assert(std::is_base_of_v<I2cReadCommand, T>,
                  "Only classes derived from I2cReadCommand can use Create");

    if (read_len != 1 && read_len != 2 && read_len != 4) {
      return nullptr;
    }

    auto cmd =
        I2cPassthruCommand::Create<T>(port, addr8 >> 1, {offset}, read_len);

    cmd->read_len_ = read_len;
    return cmd;
  }

  virtual uint32_t Data() const;

 private:
  uint8_t read_len_;
};

static_assert(!std::is_copy_constructible_v<I2cReadCommand>,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable_v<I2cReadCommand>,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_I2C_READ_COMMAND_H_

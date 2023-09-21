// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_I2C_PASSTHRU_COMMAND_H_
#define LIBEC_I2C_PASSTHRU_COMMAND_H_

#include <algorithm>
#include <memory>
#include <type_traits>
#include <vector>

#include <base/containers/span.h>
#include <brillo/brillo_export.h>

#include "libec/ec_command.h"
#include "libec/i2c_passthru_params.h"

namespace ec {

class BRILLO_EXPORT I2cPassthruCommand
    : public EcCommand<i2c_passthru::Params, i2c_passthru::Response> {
 public:
  // Use factory method instead.
  I2cPassthruCommand() : EcCommand(EC_CMD_I2C_PASSTHRU) {}

  ~I2cPassthruCommand() override = default;

  // Factory method.
  // @param port I2C port number
  // @param addr I2C target address in 7-bit
  // @param write_data data to write
  // @param read_len number of bytes to read
  // @return a pointer to the command or |nullptr| if error
  template <typename T = I2cPassthruCommand>
  static std::unique_ptr<T> Create(uint8_t port,
                                   uint8_t addr,
                                   const std::vector<uint8_t>& write_data,
                                   size_t read_len) {
    static_assert(
        std::is_base_of_v<I2cPassthruCommand, T>,
        "Only classes derived from I2cPassthruCommand can use Create");

    auto cmd = std::make_unique<T>();
    i2c_passthru::Params request{
        .req = {.port = port,
                .num_msgs = static_cast<uint8_t>((write_data.size() > 0) +
                                                 (read_len > 0))}};
    CHECK_LE(request.req.num_msgs, 2);

    using PassthruMessage = struct ec_params_i2c_passthru_msg;
    constexpr size_t message_size = realsizeof<PassthruMessage>;
    size_t req_size = realsizeof<decltype(request.req)> +
                      message_size * request.req.num_msgs + write_data.size();
    size_t resp_size = realsizeof<decltype(cmd->Resp()->resp)> + read_len;

    if (req_size > kMaxPacketSize) {
      LOG(ERROR) << "write_data size (" << static_cast<int>(write_data.size())
                 << ") too large";
      return nullptr;
    }
    if (read_len > i2c_passthru::kResponseDataMaxSize) {
      LOG(ERROR) << "read_len (" << static_cast<int>(read_len)
                 << ") should not be greater than "
                 << i2c_passthru::kResponseDataMaxSize;
      return nullptr;
    }

    base::span<PassthruMessage> messages(
        reinterpret_cast<PassthruMessage*>(request.msg_and_payload.data()),
        request.req.num_msgs);
    auto message_it = messages.begin();
    if (write_data.size() > 0) {
      message_it->addr_flags = addr;
      message_it->len = write_data.size();
      auto payload = request.msg_and_payload.begin() + messages.size_bytes();
      std::copy(write_data.begin(), write_data.end(), payload);
      ++message_it;
    }
    if (read_len > 0) {
      message_it->addr_flags = addr | EC_I2C_FLAG_READ;
      message_it->len = read_len;
    }

    cmd->SetReq(request);
    cmd->SetReqSize(req_size);
    cmd->SetRespSize(resp_size);
    return cmd;
  }

  // Returns the status code from the response of the I2C command.
  virtual uint8_t I2cStatus() const { return Resp()->resp.i2c_status; }

  // Returns a byte array containing the data from the response of the
  // I2C command.
  virtual base::span<const uint8_t> RespData() const;
};

static_assert(!std::is_copy_constructible_v<I2cPassthruCommand>,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable_v<I2cPassthruCommand>,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_I2C_PASSTHRU_COMMAND_H_

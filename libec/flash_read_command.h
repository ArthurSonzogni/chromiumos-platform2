// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_READ_COMMAND_H_
#define LIBEC_FLASH_READ_COMMAND_H_

#include <array>
#include <limits>
#include <memory>
#include <vector>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include "libec/ec_command.h"

namespace ec {

using FlashReadPacket = std::array<uint8_t, kMaxPacketSize>;

class BRILLO_EXPORT FlashReadCommand
    : public EcCommand<struct ec_params_flash_read, FlashReadPacket> {
 public:
  template <typename T = FlashReadCommand>
  static std::unique_ptr<T> Create(uint32_t offset,
                                   uint32_t read_size,
                                   uint16_t max_packet_size) {
    static_assert(std::is_base_of<FlashReadCommand, T>::value,
                  "Only classes derived from FlashReadCommand can use Create");

    if (read_size == 0) {
      return nullptr;
    }

    if (max_packet_size == 0 || max_packet_size > kMaxPacketSize) {
      return nullptr;
    }

    if (read_size >
        static_cast<uint64_t>(std::numeric_limits<decltype(offset)>::max()) -
            offset + 1) {
      return nullptr;
    }

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    return base::WrapUnique(new T(offset, read_size, max_packet_size));
  }

  ~FlashReadCommand() override = default;

  bool Run(int fd) override;
  std::vector<uint8_t> GetData() const;

 protected:
  FlashReadCommand(uint32_t offset,
                   uint32_t read_size,
                   uint16_t max_packet_size);
  virtual bool EcCommandRun(int fd);

 private:
  std::vector<uint8_t> read_data_;
  const uint32_t offset_;
  const uint16_t max_packet_size_;
};

static_assert(!std::is_copy_constructible<FlashReadCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FlashReadCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FLASH_READ_COMMAND_H_

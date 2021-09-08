// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FLASH_WRITE_COMMAND_H_
#define LIBEC_FLASH_WRITE_COMMAND_H_

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include "libec/ec_command.h"
#include "libec/flash_write_params.h"

namespace ec {

class BRILLO_EXPORT FlashWriteCommand
    : public EcCommand<flash_write::Params, EmptyParam> {
 public:
  template <typename T = FlashWriteCommand>
  static std::unique_ptr<T> Create(std::vector<uint8_t> data,
                                   uint32_t offset,
                                   uint16_t max_packet_size) {
    static_assert(std::is_base_of<FlashWriteCommand, T>::value,
                  "Only classes derived from FlashWriteCommand can use Create");

    if (data.empty()) {
      return nullptr;
    }

    if (max_packet_size == 0 || max_packet_size > kMaxPacketSize) {
      return nullptr;
    }

    if (data.size() >
        static_cast<uint64_t>(std::numeric_limits<decltype(offset)>::max()) -
            offset + 1) {
      return nullptr;
    }

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    return base::WrapUnique(new T(std::move(data), offset, max_packet_size));
  }

  ~FlashWriteCommand() override = default;

  bool Run(int fd) override;

 protected:
  FlashWriteCommand(std::vector<uint8_t> data,
                    uint32_t offset,
                    uint16_t max_packet_size)
      : EcCommand(EC_CMD_FLASH_WRITE),
        write_data_(std::move(data)),
        offset_(offset),
        max_packet_size_(max_packet_size) {}
  virtual bool EcCommandRun(int fd);

 private:
  std::vector<uint8_t> write_data_;
  uint32_t offset_;
  uint16_t max_packet_size_;
};

static_assert(!std::is_copy_constructible<FlashWriteCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FlashWriteCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FLASH_WRITE_COMMAND_H_

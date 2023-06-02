// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_READ_MEMMAP_COMMAND_H_
#define LIBEC_READ_MEMMAP_COMMAND_H_

#include <algorithm>

#include "libec/ec_command.h"

namespace ec {

template <typename Response>
class BRILLO_EXPORT ReadMemmapCommand
    : public EcCommand<struct ec_params_read_memmap, Response> {
 public:
  explicit ReadMemmapCommand(uint8_t offset)
      : EcCommand<struct ec_params_read_memmap, Response>(
            EC_CMD_READ_MEMMAP,
            kVersionZero,
            {.offset = offset, .size = sizeof(Response)}),
        offset_(offset) {
    this->SetRespSize(sizeof(Response));
  }
  ~ReadMemmapCommand() override = default;

  bool Run(int fd) override {
    if (ReadMemmapUsingIoctl(fd, offset_, bytes_, this->Resp())) {
      return true;
    }

    return EcCommandRun(fd);
  }

  virtual bool EcCommandRun(int fd) {
    return EcCommand<struct ec_params_read_memmap, Response>::Run(fd);
  }

 private:
  virtual int IoctlReadmem(int fd, uint32_t request, cros_ec_readmem_v2* data) {
    return ::ioctl(fd, request, data);
  }

  bool ReadMemmapUsingIoctl(int fd, int offset, int bytes, void* dest) {
    struct cros_ec_readmem_v2 ioctl_mem_buffer;
    ioctl_mem_buffer.offset = offset;
    ioctl_mem_buffer.bytes = bytes;

    int ret = IoctlReadmem(fd, CROS_EC_DEV_IOCRDMEM_V2, &ioctl_mem_buffer);

    if (ret >= 0) {
      std::copy(ioctl_mem_buffer.buffer, ioctl_mem_buffer.buffer + bytes,
                static_cast<uint8_t*>(dest));
      return true;
    }
    return false;
  }

  constexpr static uint8_t bytes_ = sizeof(Response);
  uint8_t offset_;
};

using ReadMemmapMem8Command = ReadMemmapCommand<uint8_t>;

static_assert(!std::is_copy_constructible<ReadMemmapMem8Command>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<ReadMemmapMem8Command>::value,
              "EcCommands are not copy-assignable by default");

using ReadMemmapMem16Command = ReadMemmapCommand<uint16_t>;

static_assert(!std::is_copy_constructible<ReadMemmapMem16Command>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<ReadMemmapMem16Command>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_READ_MEMMAP_COMMAND_H_

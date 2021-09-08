// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_COMMAND_H_
#define LIBEC_EC_COMMAND_H_

#include <sys/ioctl.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <base/logging.h>
#include <base/macros.h>
#include <chromeos/ec/cros_ec_dev.h>

namespace ec {

// Character device exposing the EC command interface.
constexpr char kCrosEcPath[] = "/dev/cros_ec";

enum class EcCmdVersionSupportStatus {
  UNKNOWN = 0,
  SUPPORTED = 1,
  UNSUPPORTED = 2,
};

// Upper bound of the host command packet transfer size. Although the EC can
// request a smaller transfer size, this value should never be smaller than
// the largest size the EC can transfer; this value is used to create buffers
// to hold the data to be transferred to and from the EC.
//
// The standard transfer size for v3 commands is is big enough to handle a
// request/response header, flash write offset/size, and 512 bytes of flash
// data:
//   sizeof(ec_host_request):          8
//   sizeof(ec_params_flash_write):    8
//   payload                         512
//                                 = 544 (0x220)
// See
// https://source.chromium.org/chromiumos/_/chromium/chromiumos/platform/ec/+/f3ffccd7d0fe4d0ce60434310795a7bfdaa5274c:chip/stm32/spi.c;l=82;drc=dede4e01ae4c877bb05d671087a6e85a29a0f902
// https://source.chromium.org/chromiumos/_/chromium/chromiumos/platform/ec/+/f3ffccd7d0fe4d0ce60434310795a7bfdaa5274c:chip/npcx/shi.c;l=118;drc=2a5ce905c11807a19035f7a072489df04be4db97
constexpr static int kMaxPacketSize = 544;

// Empty request or response for the EcCommand template below.
struct EmptyParam {};
// empty struct is one byte in C++, get the size we want instead.
template <typename T>
constexpr size_t realsizeof() {
  return std::is_empty<T>::value ? 0 : sizeof(T);
}

constexpr uint32_t kVersionZero = 0;
constexpr uint32_t kVersionOne = 1;

static constexpr auto kEcCommandUninitializedResult =
    std::numeric_limits<uint32_t>::max();

class EcCommandInterface {
 public:
  virtual ~EcCommandInterface() = default;
  virtual bool Run(int fd) = 0;
  virtual bool RunWithMultipleAttempts(int fd, int num_attempts) = 0;
  virtual uint32_t Version() const = 0;
  virtual uint32_t Command() const = 0;
};

// Helper to build and send the command structures for cros_fp.
template <typename O, typename I>
class EcCommand : public EcCommandInterface {
 public:
  explicit EcCommand(uint32_t cmd, uint32_t ver = 0, const O& req = {})
      : data_({
            .cmd = {.version = ver,
                    .command = cmd,
                    .outsize = realsizeof<O>(),
                    .insize = realsizeof<I>(),
                    .result = kEcCommandUninitializedResult},
            .req = req,
        }) {}
  EcCommand(const EcCommand&) = delete;
  EcCommand& operator=(const EcCommand&) = delete;

  ~EcCommand() override = default;

  void SetRespSize(uint32_t insize) { data_.cmd.insize = insize; }
  void SetReqSize(uint32_t outsize) { data_.cmd.outsize = outsize; }
  void SetReq(const O& req) { data_.req = req; }

  /**
   * Run an EC command.
   *
   * @param ec_fd file descriptor for the EC device
   * @return true if command runs successfully and response size is same as
   * expected, false otherwise
   *
   * The caller must be careful to only retry EC state-less
   * commands, that can be rerun without consequence.
   */
  bool Run(int ec_fd) override;

  bool RunWithMultipleAttempts(int fd, int num_attempts) override;

  virtual I* Resp() { return &data_.resp; }
  virtual const I* Resp() const { return &data_.resp; }
  uint32_t RespSize() const { return data_.cmd.insize; }
  O* Req() { return &data_.req; }
  const O* Req() const { return &data_.req; }
  virtual uint32_t Result() const { return data_.cmd.result; }

  uint32_t Version() const override { return data_.cmd.version; }
  uint32_t Command() const override { return data_.cmd.command; }

  struct Data {
    struct cros_ec_command_v2 cmd;
    union {
      O req;
      I resp;
    };
  };

 protected:
  bool ErrorTypeCanBeRetried(uint32_t ec_cmd_result);
  Data data_;

 private:
  virtual int ioctl(int fd, uint32_t request, Data* data) {
    return ::ioctl(fd, request, data);
  }
};

template <typename O, typename I>
bool EcCommand<O, I>::Run(int ec_fd) {
  data_.cmd.result = kEcCommandUninitializedResult;

  // We rely on the ioctl preserving data_.req when the command fails.
  // This is important for subsequent retries using the same data_.req.
  int ret = ioctl(ec_fd, CROS_EC_DEV_IOCXCMD_V2, &data_);
  if (ret < 0) {
    // If the ioctl fails for some reason let's make sure that the driver
    // didn't touch the result.
    data_.cmd.result = kEcCommandUninitializedResult;
    PLOG(ERROR) << "FPMCU ioctl command 0x" << std::hex << data_.cmd.command
                << std::dec << " failed";
    return false;
  }

  return (static_cast<uint32_t>(ret) == data_.cmd.insize);
}

template <typename O, typename I>
bool EcCommand<O, I>::RunWithMultipleAttempts(int fd, int num_attempts) {
  for (int retry = 0; retry < num_attempts; retry++) {
    bool ret = Run(fd);

    if (ret) {
      LOG_IF(INFO, retry > 0)
          << "FPMCU ioctl command 0x" << std::hex << data_.cmd.command
          << std::dec << " succeeded on attempt " << retry + 1 << "/"
          << num_attempts << ".";
      return true;
    }

    if (!ErrorTypeCanBeRetried(Result()) || (errno != ETIMEDOUT)) {
      LOG(ERROR) << "FPMCU ioctl command 0x" << std::hex << data_.cmd.command
                 << std::dec << " failed on attempt " << retry + 1 << "/"
                 << num_attempts << ", retry is not allowed for error";
      return false;
    }

    LOG(ERROR) << "FPMCU ioctl command 0x" << std::hex << data_.cmd.command
               << std::dec << " failed on attempt " << retry + 1 << "/"
               << num_attempts;
  }
  return false;
}

template <typename O, typename I>
bool EcCommand<O, I>::ErrorTypeCanBeRetried(uint32_t ec_cmd_result) {
  switch (ec_cmd_result) {
    case kEcCommandUninitializedResult:
    case EC_RES_TIMEOUT:
    case EC_RES_BUSY:
      return true;
    default:
      return false;
  }
}

}  // namespace ec

#endif  // LIBEC_EC_COMMAND_H_

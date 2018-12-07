// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BIOD_CROS_FP_DEVICE_H_
#define BIOD_CROS_FP_DEVICE_H_

#include <sys/ioctl.h>

#include <bitset>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/message_loop/message_loop.h>
#include <chromeos/ec/cros_ec_dev.h>
#include <chromeos/ec/ec_commands.h>

#include "biod/uinput_device.h"

using MessageLoopForIO = base::MessageLoopForIO;

using VendorTemplate = std::vector<uint8_t>;

namespace biod {

class CrosFpDevice : public MessageLoopForIO::Watcher {
 public:
  using MkbpCallback = base::Callback<void(const uint32_t event)>;

  explicit CrosFpDevice(const MkbpCallback& mkbp_event)
      : mkbp_event_(mkbp_event),
        fd_watcher_(std::make_unique<MessageLoopForIO::FileDescriptorWatcher>(
            FROM_HERE)) {}
  ~CrosFpDevice();

  static std::unique_ptr<CrosFpDevice> Open(const MkbpCallback& callback);

  // Run a simple command to get the version information from FP MCU and check
  // whether the image type returned is the same as |expected_image|.
  static bool WaitOnEcBoot(const base::ScopedFD& cros_fp_fd,
                           ec_current_image expected_image);

  bool FpMode(uint32_t mode);
  bool GetFpMode(uint32_t* mode);
  bool GetFpStats(int* capture_ms, int* matcher_ms, int* overall_ms);
  bool GetDirtyMap(std::bitset<32>* bitmap);
  bool GetTemplate(int index, VendorTemplate* out);
  bool UploadTemplate(const VendorTemplate& tmpl);
  bool SetContext(std::string user_id);
  bool ResetContext();
  // Initialise the entropy in the SBP. If |reset| is true, the old entropy
  // will be deleted. If |reset| is false, we will only add entropy, and only
  // if no entropy had been added before.
  bool InitEntropy(bool reset = false);

  int MaxTemplateCount() { return info_.template_max; }
  int TemplateVersion() { return info_.template_version; }

  // Kernel device exposing the MCU command interface.
  static constexpr char kCrosFpPath[] = "/dev/cros_fp";

  static constexpr int kLastTemplate = -1;

 protected:
  // MessageLoopForIO::Watcher overrides:
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override {}

 private:
  bool Init();

  bool EcDevInit();
  bool EcProtoInfo(ssize_t* max_read, ssize_t* max_write);
  bool EcReboot(ec_current_image to_image);
  // Run the EC command to generate new entropy in the underlying MCU.
  // |reset| specifies whether we want to merely add entropy (false), or
  // perform a reset, which erases old entropy(true).
  bool AddEntropy(bool reset);
  // Get block id from rollback info.
  bool GetRollBackInfoId(int32_t* block_id);
  bool SetUpFp();
  bool FpFrame(int index, std::vector<uint8_t>* frame);
  bool UpdateFpInfo();
  // Run a sequence of EC commands to update the entropy in the
  // MCU. If |reset| is set to true, it will additionally erase the existing
  // entropy too.
  bool UpdateEntropy(bool reset);

  base::ScopedFD cros_fd_;
  ssize_t max_read_size_;
  ssize_t max_write_size_;
  struct ec_response_fp_info info_;

  MkbpCallback mkbp_event_;
  UinputDevice input_device_;

  std::unique_ptr<MessageLoopForIO::FileDescriptorWatcher> fd_watcher_;

  DISALLOW_COPY_AND_ASSIGN(CrosFpDevice);
};

// Empty request or response for the EcCommand template below.
struct EmptyParam {};
// empty struct is one byte in C++, get the size we want instead.
template <typename T>
constexpr size_t realsizeof() {
  return std::is_empty<T>::value ? 0 : sizeof(T);
}

// Helper to build and send the command structures for cros_fp.
template <typename O, typename I>
class EcCommand {
 public:
  explicit EcCommand(uint32_t cmd, uint32_t ver = 0, const O& req = {})
      : data_({
            .cmd = {.version = ver,
                    .command = cmd,
                    .result = 0xff,
                    .outsize = realsizeof<O>(),
                    .insize = realsizeof<I>()},
            .req = req,
        }) {}

  void SetRespSize(uint32_t insize) { data_.cmd.insize = insize; }
  void SetReqSize(uint32_t outsize) { data_.cmd.outsize = outsize; }
  void SetReq(const O& req) { data_.req = req; }

  bool Run(int ec_fd) {
    data_.cmd.result = 0xff;
    int result = ioctl(ec_fd, CROS_EC_DEV_IOCXCMD_V2, &data_);
    if (result < 0) {
      PLOG(ERROR) << "FPMCU ioctl failed, command: " << data_.cmd.command;
      return false;
    }

    return (static_cast<uint32_t>(result) == data_.cmd.insize);
  }

  I* Resp() { return &data_.resp; }
  O* Req() { return &data_.req; }
  uint16_t Result() { return data_.cmd.result; }

 private:
  struct {
    struct cros_ec_command_v2 cmd;
    union {
      O req;
      I resp;
    };
  } data_;

  DISALLOW_COPY_AND_ASSIGN(EcCommand);
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_H_

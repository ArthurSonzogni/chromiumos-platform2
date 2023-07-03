// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hammerd/i2c_endpoint.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <base/threading/platform_thread.h>
#include <re2/re2.h>

#include "hammerd/update_fw.h"

namespace hammerd {

namespace {
constexpr int kUsbUpdaterWriteReg = 0x10;
constexpr int kUsbUpdaterReadReg = 0x11;
}  // namespace

bool I2CEndpoint::UsbSysfsExists() {
  base::FilePath path{"/sys/bus/i2c/devices/"};
  path = path.Append(i2c_path_);
  return base::DirectoryExists(path);
}

UsbConnectStatus I2CEndpoint::Connect(bool check_id) {
  RE2 pattern("([1-9][0-9]*)-([[:xdigit:]]{4})");
  int bus;
  std::string addr;

  if (!RE2::FullMatch(i2c_path_, pattern, &bus, &addr)) {
    return UsbConnectStatus::kUnknownError;
  }

  if (!base::HexStringToUInt(addr, &addr_)) {
    return UsbConnectStatus::kUnknownError;
  }

  if (!UsbSysfsExists()) {
    return UsbConnectStatus::kUsbPathEmpty;
  }

  std::string dev_path = base::StringPrintf("/dev/i2c-%d", bus);
  fd_ = open(dev_path.c_str(), O_RDWR);
  if (fd_ < 0) {
    return UsbConnectStatus::kUnknownError;
  }
  ioctl(fd_, I2C_SLAVE, addr_);

  // get firmware version
  alignas(UpdateFrameHeader) char
      request[sizeof(UpdateFrameHeader) + sizeof(uint16_t)];
  // 1 byte error code + 3 bytes "RO:" or "RW:" + 32 bytes version string
  char response[36] = {};

  reinterpret_cast<UpdateFrameHeader&>(request) =
      UpdateFrameHeader(sizeof(request), 0, kUpdateExtraCmd);
  reinterpret_cast<uint16_t&>(request[sizeof(UpdateFrameHeader)]) =
      htobe16((uint16_t)UpdateExtraCommand::kGetVersionString);
  int ret = Transfer(&request, sizeof(request), response, sizeof(response));

  if (ret < 0) {
    // Old EC may return kInvalidCommand.
    // Ignore this error so the updater can update the fw to a newer version
    // that supports kGetVersionString
    if (response[0] == static_cast<int>(EcResponseStatus::kInvalidCommand)) {
      configuration_string_ = "<unknown>";
    } else {
      return UsbConnectStatus::kUnknownError;
    }
  } else {
    response[sizeof(response) - 1] = '\0';
    configuration_string_ = response + 1;
  }

  return UsbConnectStatus::kSuccess;
}

void I2CEndpoint::Close() {
  if (IsConnected()) {
    close(fd_);
    fd_ = -1;
    configuration_string_ = "";
  }
}

int I2CEndpoint::Send(const void* outbuf,
                      int outlen,
                      bool allow_less,
                      unsigned int timeout_ms) {
  if (outlen == 0) {
    return 0;
  }
  if (outbuf == nullptr) {
    return -1;
  }

  std::vector<uint8_t> out(outlen + 1);
  std::copy(reinterpret_cast<const uint8_t*>(outbuf),
            reinterpret_cast<const uint8_t*>(outbuf) + outlen, out.data() + 1);
  out[0] = kUsbUpdaterWriteReg;

  i2c_msg msg = {static_cast<uint16_t>(addr_), 0,
                 static_cast<uint16_t>(outlen + 1), out.data()};
  i2c_rdwr_ioctl_data msgset = {&msg, 1};
  int ret = ioctl(fd_, I2C_RDWR, &msgset);
  if (ret < 0) {
    LOG(ERROR) << "ioctl error " << ret;
  }
  return ret >= 0 ? outlen : -1;
}

int I2CEndpoint::ReceiveNoWait(void* inbuf, int inlen) {
  if (inlen == 0) {
    return 0;
  }
  if (inbuf == nullptr) {
    return -1;
  }

  std::vector<uint8_t> in(inlen + 1);

  uint8_t outreg = kUsbUpdaterReadReg;
  i2c_msg msg[2] = {
      {static_cast<uint16_t>(addr_), 0, 1, &outreg},
      {static_cast<uint16_t>(addr_), I2C_M_RD, (uint16_t)(inlen + 1),
       in.data()},
  };

  i2c_rdwr_ioctl_data msgset = {msg, 2};
  int ret = ioctl(fd_, I2C_RDWR, &msgset);
  if (ret < 0) {
    LOG(ERROR) << "ioctl error " << ret;
  }
  std::copy(in.begin() + 1, in.end(), reinterpret_cast<uint8_t*>(inbuf));
  return ret >= 0 ? in[0] : -1;
}

int I2CEndpoint::Receive(void* inbuf,
                         int inlen,
                         bool allow_less,
                         unsigned int timeout_ms) {
  constexpr unsigned int kIntervalMs = 100;

  if (timeout_ms == 0) {
    timeout_ms = 1000;
  }

  auto end = base::Time::Now() + base::Milliseconds(timeout_ms);
  int total_read = 0;

  while (base::Time::Now() < end && inlen > 0) {
    int ret = ReceiveNoWait(inbuf, inlen);

    if (ret < 0) {
      return ret;
    }

    inbuf = reinterpret_cast<uint8_t*>(inbuf) + ret;
    inlen -= ret;
    total_read += ret;

    if (ret > 0 && allow_less) {
      break;
    }

    base::PlatformThread::Sleep(base::Milliseconds(kIntervalMs));
  }

  return (allow_less || inlen == 0) ? total_read : -1;
}
}  // namespace hammerd

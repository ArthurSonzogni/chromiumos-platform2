// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_MBIM_CMD_H_
#define HERMES_MBIM_CMD_H_

#include <cstdint>
#include <base/check.h>
#include <base/logging.h>

class MbimCmd {
 public:
  // enum values are borrowed from QMI for consistency
  enum MbimType : uint16_t {
    kMbimSubscriberStatusReady = 0x00,
    kMbimSendApdu = 0x3B,
    kMbimOpenLogicalChannel = 0x42,
    kMbimCloseLogicalChannel = 0x46,
    kMbimDeviceCaps = 0x47,
    kMbimSendEidApdu = 0x50,
  };
  explicit MbimCmd(MbimType mbim_type) : mbim_type_(mbim_type) {}

  uint16_t mbim_type() { return static_cast<uint16_t>(mbim_type_); }

 private:
  MbimType mbim_type_;
};

#endif  // HERMES_MBIM_CMD_H_

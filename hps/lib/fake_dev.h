// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Fake device for HPS testing.
 */
#ifndef HPS_LIB_FAKE_DEV_H_
#define HPS_LIB_FAKE_DEV_H_

#include <memory>
#include <vector>

#include "hps/lib/dev.h"
#include "hps/lib/hps_reg.h"

namespace hps {

class DevImpl;

class FakeDev : public DevInterface {
 public:
  virtual ~FakeDev();
  // Device interface
  bool Read(uint8_t cmd, uint8_t* data, size_t len) override;
  bool Write(uint8_t cmd, const uint8_t* data, size_t len) override;
  // Flags for controlling behaviour. Multiple flags can be set,
  // controlling how the fake responds under test conditions.
  enum Flags : uint32_t {
    kNone = 0,
    kBootFault = 1u << 0,
    kApplNotVerified = 1u << 1,
    kSpiNotVerified = 1u << 2,
    kWpOff = 1u << 3,
    kMemFail = 1u << 4,
    kSkipBoot = 1u << 5,
  };
  void Start(enum Flags flags);
  static std::unique_ptr<DevInterface> Create(enum Flags flags);
  // TODO(amcrae): Add an interface to retrieve memory data written
  // to the device.

 private:
  FakeDev();
  std::unique_ptr<DevImpl> device_;
};

}  // namespace hps

#endif  // HPS_LIB_FAKE_DEV_H_

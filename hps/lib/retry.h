// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Intermediate device that implements retries
 */
#ifndef HPS_LIB_RETRY_H_
#define HPS_LIB_RETRY_H_

#include <memory>
#include <utility>

#include <base/time/time.h>

#include "hps/lib/dev.h"

namespace hps {

class RetryDev : public DevInterface {
 public:
  RetryDev(std::unique_ptr<DevInterface> dev,
           int retries,
           const base::TimeDelta& delay)
      : device_(std::move(dev)), retries_(retries), delay_(delay) {}
  ~RetryDev() {}
  bool Read(uint8_t cmd, uint8_t* data, size_t len) override;
  bool Write(uint8_t cmd, const uint8_t* data, size_t len) override;

 private:
  std::unique_ptr<DevInterface> device_;
  int retries_;
  base::TimeDelta delay_;
};

}  // namespace hps

#endif  // HPS_LIB_RETRY_H_

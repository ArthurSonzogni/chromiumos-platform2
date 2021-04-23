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
#include <vector>

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
  bool read(uint8_t cmd, std::vector<uint8_t>* data) override;
  bool write(uint8_t cmd, const std::vector<uint8_t>& data) override;

 private:
  std::unique_ptr<DevInterface> device_;
  int retries_;
  base::TimeDelta delay_;
};

}  // namespace hps

#endif  // HPS_LIB_RETRY_H_

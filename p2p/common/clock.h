// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef P2P_COMMON_CLOCK_H__
#define P2P_COMMON_CLOCK_H__

#include "p2p/common/clock_interface.h"

#include <base/macros.h>

namespace p2p {

namespace common {

class Clock : public ClockInterface {
 public:
  Clock() {}

  virtual void Sleep(const base::TimeDelta& duration);

  virtual base::Time GetMonotonicTime();

 private:
  DISALLOW_COPY_AND_ASSIGN(Clock);
};

}  // namespace common

}  // namespace p2p

#endif  // P2P_COMMON_CLOCK_H__

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SYSTEM_H_
#define PATCHPANEL_SYSTEM_H_

#include <net/if.h>
#include <net/route.h>
#include <sys/ioctl.h>

namespace patchpanel {

// cros lint will yell to force using int16/int64 instead of long here, however
// note that unsigned long IS the correct signature for ioctl in Linux kernel -
// it's 32 bits on 32-bit platform and 64 bits on 64-bit one.
using ioctl_req_t = unsigned long;  // NOLINT(runtime/int)

// Stateless class used for holding all utility functions with side
// effects on the environment. Facilitates mocking these functions in unit
// tests.
class System {
 public:
  System() = default;
  System(const System&) = delete;
  System& operator=(const System&) = delete;
  virtual ~System() = default;

  virtual int Ioctl(int fd, ioctl_req_t request, const char* argp);
  int Ioctl(int fd, ioctl_req_t request, uint64_t arg);
  int Ioctl(int fd, ioctl_req_t request, struct ifreq* ifr);
  int Ioctl(int fd, ioctl_req_t request, struct rtentry* route);

 private:
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SYSTEM_H_

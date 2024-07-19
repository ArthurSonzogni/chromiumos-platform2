// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NOOP_SYSTEM_H_
#define PATCHPANEL_NOOP_SYSTEM_H_

#include "patchpanel/system.h"

namespace patchpanel {

// Stub the System class that all the methods do nothing and always succeeds.
class NoopSystem : public System {
 public:
  NoopSystem() = default;
  NoopSystem(const NoopSystem&) = delete;
  NoopSystem& operator=(const NoopSystem&) = delete;
  ~NoopSystem() override = default;

  int Ioctl(int fd, ioctl_req_t request, const char* argp) override {
    return 0;
  }
  int Ioctl(int fd, ioctl_req_t request, uint64_t arg) override { return 0; }
  int Ioctl(int fd, ioctl_req_t request, struct ifreq* ifr) override {
    return 0;
  }
  int Ioctl(int fd, ioctl_req_t request, struct rtentry* route) override {
    return 0;
  }
  int Ioctl(int fd, ioctl_req_t request, struct in6_rtmsg* route) override {
    return 0;
  }
};

}  // namespace patchpanel

#endif  // PATCHPANEL_NOOP_SYSTEM_H_

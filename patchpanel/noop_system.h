// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NOOP_SYSTEM_H_
#define PATCHPANEL_NOOP_SYSTEM_H_

#include <memory>

#include "patchpanel/system.h"

namespace patchpanel {

// Stub the System class that all the methods do nothing and always succeeds.
class NoopSystem : public System {
 public:
  class NoopScopedNS : public ScopedNS {};

  NoopSystem() = default;
  NoopSystem(const NoopSystem&) = delete;
  NoopSystem& operator=(const NoopSystem&) = delete;
  ~NoopSystem() override = default;

  bool SysNetSet(SysNet target,
                 std::string_view content,
                 std::string_view iface) override {
    return true;
  }

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

  int Chown(const char* pathname, uid_t owner, gid_t group) override {
    return 0;
  }

  std::unique_ptr<ScopedNS> EnterMountNS(pid_t pid) override {
    return std::make_unique<NoopScopedNS>();
  }
  std::unique_ptr<ScopedNS> EnterNetworkNS(pid_t pid) override {
    return std::make_unique<NoopScopedNS>();
  }
  std::unique_ptr<ScopedNS> EnterNetworkNS(
      std::string_view netns_name) override {
    return std::make_unique<NoopScopedNS>();
  }
};

}  // namespace patchpanel

#endif  // PATCHPANEL_NOOP_SYSTEM_H_

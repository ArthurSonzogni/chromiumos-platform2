// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/system.h"

namespace patchpanel {

int System::Ioctl(int fd, ioctl_req_t request, const char* argp) {
  return ioctl(fd, request, argp);
}

int System::Ioctl(int fd, ioctl_req_t request, uint64_t arg) {
  return Ioctl(fd, request, reinterpret_cast<const char*>(arg));
}

int System::Ioctl(int fd, ioctl_req_t request, struct ifreq* ifr) {
  return Ioctl(fd, request, reinterpret_cast<const char*>(ifr));
}

int System::Ioctl(int fd, ioctl_req_t request, struct rtentry* route) {
  return Ioctl(fd, request, reinterpret_cast<const char*>(route));
}

}  // namespace patchpanel

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/ares_interface.h"

namespace net_base {

AresInterface* AresInterface::GetInstance() {
  static base::NoDestructor<AresInterface> instance;
  return instance.get();
}

AresInterface::AresInterface() = default;
AresInterface::~AresInterface() = default;

int AresInterface::init_options(ares_channel* channelptr,
                                struct ares_options* options,
                                int optmask) {
  return ares_init_options(channelptr, options, optmask);
}

void AresInterface::destroy(ares_channel channel) {
  return ares_destroy(channel);
}

void AresInterface::gethostbyname(ares_channel channel,
                                  const char* name,
                                  int family,
                                  ares_host_callback callback,
                                  void* arg) {
  return ares_gethostbyname(channel, name, family, callback, arg);
}

struct timeval* AresInterface::timeout(ares_channel channel,
                                       struct timeval* maxtv,
                                       struct timeval* tv) {
  return ares_timeout(channel, maxtv, tv);
}

int AresInterface::getsock(ares_channel channel,
                           ares_socket_t* socks,
                           int numsocks) {
  return ares_getsock(channel, socks, numsocks);
}

void AresInterface::process_fd(ares_channel channel,
                               ares_socket_t read_fd,
                               ares_socket_t write_fd) {
  return ares_process_fd(channel, read_fd, write_fd);
}

}  // namespace net_base

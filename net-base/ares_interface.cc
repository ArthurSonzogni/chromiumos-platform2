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

void AresInterface::set_local_dev(ares_channel channel,
                                  const char* local_dev_name) {
  return ares_set_local_dev(channel, local_dev_name);
}

void AresInterface::getaddrinfo(ares_channel channel,
                                const char* name,
                                const char* service,
                                const struct ares_addrinfo_hints* hints,
                                ares_addrinfo_callback callback,
                                void* arg) {
  return ares_getaddrinfo(channel, name, service, hints, callback, arg);
}

void AresInterface::freeaddrinfo(struct ares_addrinfo* ai) {
  return ares_freeaddrinfo(ai);
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

int AresInterface::set_servers_csv(ares_channel channel, const char* servers) {
  return ares_set_servers_csv(channel, servers);
}

}  // namespace net_base

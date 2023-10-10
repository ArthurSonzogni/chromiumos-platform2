// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_ARES_INTERFACE_H_
#define NET_BASE_ARES_INTERFACE_H_

#include <ares.h>

#include <base/no_destructor.h>

#include "net-base/export.h"

namespace net_base {

// This class is only for separating the real ares calls for the ease of unit
// tests. See the document of c-ares for each function. NET_BASE_EXPORT is
// necessary since the unit test binary also load libnet-base as a shared
// library.
class NET_BASE_EXPORT AresInterface {
 public:
  static AresInterface* GetInstance();
  virtual ~AresInterface();

  virtual int init_options(ares_channel* channelptr,
                           struct ares_options* options,
                           int optmask);

  virtual void destroy(ares_channel channel);

  virtual void gethostbyname(ares_channel channel,
                             const char* name,
                             int family,
                             ares_host_callback callback,
                             void* arg);

  virtual struct timeval* timeout(ares_channel channel,
                                  struct timeval* maxtv,
                                  struct timeval* tv);

  virtual int getsock(ares_channel channel, ares_socket_t* socks, int numsocks);

  virtual void process_fd(ares_channel channel,
                          ares_socket_t read_fd,
                          ares_socket_t write_fd);

 protected:
  AresInterface();
  AresInterface(const AresInterface&) = delete;
  AresInterface& operator=(const AresInterface&) = delete;

 private:
  friend class base::NoDestructor<AresInterface>;
};
}  // namespace net_base

#endif  // NET_BASE_ARES_INTERFACE_H_

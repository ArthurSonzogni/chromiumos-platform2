// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_ARES_INTERFACE_H_
#define NET_BASE_ARES_INTERFACE_H_

#include <ares.h>

#include <base/no_destructor.h>
#include <brillo/brillo_export.h>

namespace net_base {

// This class is only for separating the real ares calls for the ease of unit
// tests. See the document of c-ares for each function. BRILLO_EXPORT is
// necessary since the unit test binary also load libnet-base as a shared
// library.
class BRILLO_EXPORT AresInterface {
 public:
  static AresInterface* GetInstance();
  virtual ~AresInterface();

  virtual int init_options(ares_channel* channelptr,
                           struct ares_options* options,
                           int optmask);

  virtual void destroy(ares_channel channel);

  virtual void set_local_dev(ares_channel channel, const char* local_dev_name);

  virtual void getaddrinfo(ares_channel channel,
                           const char* name,
                           const char* service,
                           const struct ares_addrinfo_hints* hints,
                           ares_addrinfo_callback callback,
                           void* arg);
  virtual void freeaddrinfo(struct ares_addrinfo* ai);

  virtual struct timeval* timeout(ares_channel channel,
                                  struct timeval* maxtv,
                                  struct timeval* tv);

  virtual int getsock(ares_channel channel, ares_socket_t* socks, int numsocks);

  virtual void process_fd(ares_channel channel,
                          ares_socket_t read_fd,
                          ares_socket_t write_fd);

  virtual int set_servers_csv(ares_channel channel, const char* servers);

 protected:
  AresInterface();
  AresInterface(const AresInterface&) = delete;
  AresInterface& operator=(const AresInterface&) = delete;

 private:
  friend class base::NoDestructor<AresInterface>;
};
}  // namespace net_base

#endif  // NET_BASE_ARES_INTERFACE_H_

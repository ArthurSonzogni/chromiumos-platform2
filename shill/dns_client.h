// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DNS_CLIENT_H_
#define SHILL_DNS_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <net-base/ip_address.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/error.h"
#include "shill/event_dispatcher.h"

struct hostent;

namespace shill {

class Ares;
struct DnsClientState;

// Implements a DNS resolution client that can run asynchronously.
class DnsClient {
 public:
  using ClientCallback = base::RepeatingCallback<void(
      const base::expected<net_base::IPAddress, Error>&)>;

  static const char kErrorNoData[];
  static const char kErrorFormErr[];
  static const char kErrorServerFail[];
  static const char kErrorNotFound[];
  static const char kErrorNotImp[];
  static const char kErrorRefused[];
  static const char kErrorBadQuery[];
  static const char kErrorNetRefused[];
  static const char kErrorTimedOut[];
  static const char kErrorUnknown[];

  // Total default timeout for the query over all tries and all name servers.
  static constexpr base::TimeDelta kDnsTimeout = base::Milliseconds(8000);
  // Minimum timeout per query to a name server.
  static constexpr base::TimeDelta kDnsQueryMinTimeout =
      base::Milliseconds(1000);
  // Total number of tries per name server.
  static constexpr int kDnsQueryTries = 2;

  DnsClient(net_base::IPFamily family,
            const std::string& interface_name,
            base::TimeDelta timeout,
            EventDispatcher* dispatcher,
            const ClientCallback& callback);
  DnsClient(const DnsClient&) = delete;
  DnsClient& operator=(const DnsClient&) = delete;

  virtual ~DnsClient();

  // Returns true if the DNS client started successfully, false otherwise.
  // If successful, the callback will be called with the result of the
  // request.  If Start() fails and returns false, the callback will not
  // be called, but the error that caused the failure will be returned in
  // |error|.
  virtual bool Start(const std::vector<std::string>& dns_list,
                     const std::string& hostname,
                     Error* error);

  // Aborts any running DNS client transaction.  This will cancel any callback
  // invocation.
  virtual void Stop();

  virtual bool IsActive() const;

  std::string interface_name() const { return interface_name_; }

 private:
  friend class DnsClientTest;

  void HandleCompletion();
  void HandleDnsRead(int fd);
  void HandleDnsWrite(int fd);
  void HandleTimeout();
  void ProcessFd(int read_fd, int write_fd);
  void ReceiveDnsReply(int status, struct hostent* hostent);
  static void ReceiveDnsReplyCB(void* arg,
                                int status,
                                int timeouts,
                                struct hostent* hostent);
  bool RefreshHandles();
  void StopReadHandlers();
  void StopWriteHandlers();

  Error error_;
  net_base::IPAddress address_;
  std::string interface_name_;
  EventDispatcher* dispatcher_;
  ClientCallback callback_;
  base::TimeDelta timeout_;
  bool running_;
  std::unique_ptr<DnsClientState> resolver_state_;
  base::CancelableOnceClosure timeout_closure_;
  base::WeakPtrFactory<DnsClient> weak_ptr_factory_;
  Ares* ares_;
};

}  // namespace shill

#endif  // SHILL_DNS_CLIENT_H_

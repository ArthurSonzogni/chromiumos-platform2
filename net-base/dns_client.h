// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_DNS_CLIENT_H_
#define NET_BASE_DNS_CLIENT_H_

#include <memory>
#include <string_view>
#include <vector>

#include <base/functional/callback.h>
#include <base/time/time.h>
#include <base/types/expected.h>

#include "net-base/ip_address.h"

namespace net_base {

class AresInterface;

// An async DNS resolver. The object can be destroyed at anytime to cancel the
// ongoing query.
class NET_BASE_EXPORT DNSClient {
 public:
  // The values should be matched with the constants defined in ares.h, except
  // for kInternal, where 0 is ARES_SUCCESS, but we don't need that status in
  // Error.
  enum class Error {
    kInternal = 0,
    kNoData = 1,
    kFormErr = 2,
    kServerFail = 3,
    kNotFound = 4,
    kNotImplemented = 5,
    kRefused = 6,
    kBadQuery = 7,
    kBadName = 8,
    kBadFamily = 9,
    kBadResp = 10,
    kNetRefused = 11,
    kTimedOut = 12,
  };

  // The callback returns the resolved IP addresses (A or AAAA records) on
  // success.
  // TODO(b/302101630): Add query latency.
  using Result = base::expected<std::vector<IPAddress>, Error>;
  using Callback = base::OnceCallback<void(const Result&)>;

  // Optional configurations for Resolve(). The DNS query behavior will follow
  // the config in resolv.conf by default.
  struct Options {
    // The maximum timeout for a single Resolve() call. Note that this is
    // independent from the timeout for a single DNS query, and the maximum
    // timeout in theory might be shorter than the value set here (e.g.,
    // when `(timeout per query) x (# retries)` is shorter).
    base::TimeDelta timeout = base::Seconds(10);
    // TODO(b/302101630): Add options for:
    // - Number of retries;
    // - Timeout of a single DNS query;
    // - Interface.
  };

  DNSClient() = default;

  // Pure virtual just to make this class abstract.
  virtual ~DNSClient() = 0;

  // DNSClientFactory is neither copyable nor movable.
  DNSClient(const DNSClient&) = delete;
  DNSClient& operator=(const DNSClient&) = delete;
};

class NET_BASE_EXPORT DNSClientFactory {
 public:
  // Resolves |hostname| to IP address in |family|. Results (either the
  // IPAddresses or failure) are returned to the caller by |callback|.
  // - This function will always return a valid object (unless it fails to
  //   allocate memory for the new object). All kind of errors will be reported
  //   via |callback|.
  // - |callback| will only be triggered after Resolve() returns.
  // - The caller can destroy the returned object at any time to cancel the
  //   ongoing DNS query. If this happens before the callback is triggered, the
  //   callback won't be triggered any more.
  // - |ares| is only used in unit tests.
  virtual std::unique_ptr<DNSClient> Resolve(IPFamily family,
                                             std::string_view hostname,
                                             DNSClient::Callback callback,
                                             const DNSClient::Options& options,
                                             AresInterface* ares = nullptr);

  DNSClientFactory() = default;
  virtual ~DNSClientFactory() = default;

  // DNSClientFactory is neither copyable nor movable.
  DNSClientFactory(const DNSClientFactory&) = delete;
  DNSClientFactory& operator=(const DNSClientFactory&) = delete;
};

}  // namespace net_base

#endif  // NET_BASE_DNS_CLIENT_H_

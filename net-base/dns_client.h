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

// An async DNS resolver.
// TODO(b/302101630): Add unit tests.
class NET_BASE_EXPORT DNSClient {
 public:
  // TODO(b/302101630): Map the enum to the values used in c-ares.
  enum class Error {
    kNoData,
    kFormErr,
    kServerFail,
    kNotFound,
    kNotImplemented,
    kRefused,
    kBadQuery,
    kNetRefused,
    kTimedOut,
    kInternal,
  };

  // The callback returns the resolved IP addresses (A or AAAA records) on
  // success.
  // TODO(b/302101630): Add query latency.
  using Result = base::expected<std::vector<IPAddress>, Error>;
  using Callback = base::OnceCallback<void(const Result&)>;

  // Optional configurations for Resolve().
  struct Options {
    base::TimeDelta timeout = base::Seconds(10);
  };

  // Resolves |hostname| to IP address in |family|. Results (either the
  // IPAddresses or failure) are returned to the caller by |callback|.
  // - This function will always return a valid object (unless it fails to
  //   allocate memory for the new object). All kind of errors will be reported
  //   via |callback|.
  // - |callback| will only be triggered after Resolve() returns.
  // - The caller can destroy the returned object at any time to cancel the
  //   ongoing DNS query. If this happens before the callback is triggered, the
  //   callback won't be triggered any more.
  static std::unique_ptr<DNSClient> Resolve(IPFamily family,
                                            std::string_view hostname,
                                            Callback callback,
                                            const Options& options);

  virtual ~DNSClient() = default;

 protected:
  // Should only be constructed from the factory method.
  DNSClient() = default;
};

}  // namespace net_base

#endif  // NET_BASE_DNS_CLIENT_H_

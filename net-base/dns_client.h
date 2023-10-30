// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_DNS_CLIENT_H_
#define NET_BASE_DNS_CLIENT_H_

#include <memory>
#include <optional>
#include <ostream>
#include <string>
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
    // ARES_ENODATA
    kNoData = 1,
    // ARES_EFORMERR
    kFormErr = 2,
    // ARES_ESERVFAIL
    kServerFail = 3,
    // ARES_ENOTFOUND
    kNotFound = 4,
    // ARES_ENOTIMP
    kNotImplemented = 5,
    // ARES_ERFUSED
    kRefused = 6,
    // ARES_EBADQUERY
    kBadQuery = 7,
    // ARES_EBADNAME
    kBadName = 8,
    // ARES_EBADFAMILY
    kBadFamily = 9,
    // ARES_EBADRESP
    kBadResp = 10,
    // ARES_ECONNREFUSED
    kNetRefused = 11,
    // ARES_ETIMEOUT
    kTimedOut = 12,
    // ARES_EOF
    kEndOfFile = 13,
    // ARES_EFILE
    kReadErr = 14,
    // ARES_ENOMEM
    kNoMemory = 15,
    // ARES_EDESTRUCTION
    kChannelDestroyed = 16,
    // ARES_EBADSTR
    kBadFormat = 17,
    // ARES_EBADFLAGS
    kBadFlags = 18,
    // ARES_ENONAME
    kBadHostname = 19,
    // ARES_EBADHINTS
    kBadHints = 20,
    // ARES_ENOTINITIALIZED
    kNotInit = 21,
    // ARES_ELOADIPHLPAPI
    kLoadErr = 22,
    // ARES_EADDRGETNETWORKPARAMS
    kGetNetworkParamsNotFound = 23,
    // ARES_ECANCELLED
    kCancelled = 24,
  };
  static std::string_view ErrorName(Error error);

  // The callback returns the resolved IP addresses (A or AAAA records) on
  // success.
  // TODO(b/302101630): Add query latency.
  using Result = base::expected<std::vector<IPAddress>, Error>;
  using Callback = base::OnceCallback<void(const Result&)>;

  // Optional configurations for Resolve().
  struct Options {
    // The maximum timeout for a single Resolve() call. Note that this is
    // independent from the timeout for a single DNS query, and the maximum
    // timeout in theory might be shorter than the value set here (e.g.,
    // when `(timeout per query) x (# tries)` is shorter).
    base::TimeDelta timeout = base::Seconds(10);

    // Maximum number of attempts to each name server. The value set in
    // resolv.conf will be used if not set.
    std::optional<int> number_of_tries;

    // The timeout value for the first try to each name server. The value set in
    // resolv.conf will be used if not set. The timeout for the following tries
    // will be controlled by the c-ares library. For more details, see comments
    // for ARES_OPT_TIMEOUTMS in https://c-ares.org/ares_init_options.html .
    std::optional<base::TimeDelta> per_query_initial_timeout;

    // If not empty, the query will be bound to this interface. Note that the
    // program needs CAP_NET_RAW to set this option.
    std::string interface;

    // The name server used for the query. The name servers in resolv.conf will
    // be used if this option in empty. Only support one name server here by
    // intention. The caller should create one DNSClient for each name server to
    // query multiple server.
    std::optional<IPAddress> name_server;
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

std::ostream& operator<<(std::ostream& stream, DNSClient::Error error);

}  // namespace net_base

#endif  // NET_BASE_DNS_CLIENT_H_

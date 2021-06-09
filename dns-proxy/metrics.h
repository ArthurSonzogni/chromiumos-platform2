// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_METRICS_H_
#define DNS_PROXY_METRICS_H_

#include <metrics/metrics_library.h>

namespace dns_proxy {

class Metrics {
 public:
  // This is not an UMA enum type.
  enum class ProcessType {
    kController,
    kProxySystem,
    kProxyDefault,
    kProxyARC,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class ProcessEvent {
    kProxyLaunchSuccess = 0,
    kProxyLaunchFailure = 1,
    kProxyKillFailure = 2,
    kProxyKilled = 3,
    kProxyStopped = 4,
    kProxyContinued = 5,
    kProxyMissing = 6,
    kCapNetBindServiceError = 7,
    kPatchpanelNotInitialized = 8,
    kPatchpanelNotReady = 9,
    kPatchpanelReset = 10,
    kPatchpanelShutdown = 11,
    kPatchpanelNoNamespace = 12,
    kPatchpanelNoRedirect = 13,
    kShillNotReady = 14,
    kShillReset = 15,
    kShillShutdown = 16,
    kShillSetProxyAddressRetryExceeded = 17,
    kChromeFeaturesNotInitialized = 18,
    kResolverListenUDPFailure = 19,
    kResolverListenTCPFailure = 20,

    kMaxValue = kResolverListenTCPFailure,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class NameserverType {
    kNone = 0,
    kIPv4 = 1,
    kIPv6 = 2,
    kBoth = 3,

    kMaxValue = kBoth,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class DnsOverHttpsMode {
    kUnknown = 0,
    kOff = 1,
    kAutomatic = 2,
    kAlwaysOn = 3,

    kMaxValue = kAlwaysOn,
  };

  // This is not an UMA enum type.
  enum class QueryType {
    kPlainText = 0,
    kDnsOverHttps = 1,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class QueryResult {
    kFailure = 0,
    kSuccess = 1,

    kMaxValue = kSuccess,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class QueryError {
    kNone = 0,
    kDomainNotFound = 1,
    kNoData = 2,
    kBadQuery = 3,
    kQueryRefused = 4,
    kQueryTimeout = 5,
    kQueryCanceled = 6,
    kConnectionRefused = 7,
    kConnectionFailed = 8,
    kUnsupportedProtocol = 9,
    kNotImplemented = 10,
    kInvalidURL = 11,
    kBadHost = 12,
    kTooManyRedirects = 13,
    kSendError = 14,
    kReceiveError = 15,
    kOtherClientError = 16,
    kOtherServerError = 17,

    kMaxValue = kOtherServerError,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class HttpError {
    kNone = 0,
    kAnyRedirect = 1,
    kBadRequest = 2,
    kPayloadTooLarge = 3,
    kURITooLong = 4,
    kUnsupportedMediaType = 5,
    kTooManyRequests = 6,
    kOtherClientError = 7,
    kNotImplemented = 8,
    kBadGateway = 9,
    kOtherServerError = 10,

    kMaxValue = kOtherServerError,
  };

  Metrics() = default;
  Metrics(const Metrics&) = delete;
  ~Metrics() = default;
  Metrics& operator=(const Metrics&) = delete;

  void RecordProcessEvent(ProcessType type, ProcessEvent event);
  void RecordNameservers(unsigned int num_ipv4, unsigned int num_ipv6);
  void RecordDnsOverHttpsMode(DnsOverHttpsMode mode);
  void RecordQueryResult(QueryType type, QueryError error, int http_code = -1);

 private:
  MetricsLibrary metrics_;
};

}  // namespace dns_proxy

#endif  // DNS_PROXY_METRICS_H_

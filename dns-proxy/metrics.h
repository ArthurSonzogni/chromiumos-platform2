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

  Metrics() = default;
  Metrics(const Metrics&) = delete;
  ~Metrics() = default;
  Metrics& operator=(const Metrics&) = delete;

  void RecordProcessEvent(ProcessType type, ProcessEvent event);
  void RecordNameservers(unsigned int num_ipv4, unsigned int num_ipv6);

 private:
  MetricsLibrary metrics_;
};

}  // namespace dns_proxy

#endif  // DNS_PROXY_METRICS_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/metrics.h"

#include <map>
#include <type_traits>

#include <base/logging.h>
#include <base/strings/string_util.h>

namespace dns_proxy {
namespace {
constexpr char kIPv4[] = "IPv4";
constexpr char kIPv6[] = "IPv6";

constexpr char kEventTemplate[] = "Network.DnsProxy.$1.Event";

constexpr char kNameserversCountTemplate[] = "Network.DnsProxy.$1Nameservers";
constexpr int kNameserversCountMax = 6;
constexpr int kNameserversCountBuckets = 5;

constexpr char kNameserverTypes[] = "Network.DnsProxy.NameserverTypes";

constexpr char kDnsOverHttpsMode[] = "Network.DnsProxy.DnsOverHttpsMode";

constexpr char kQueryResultsTemplate[] = "Network.DnsProxy.$1Query.Results";
constexpr char kQueryErrorsTemplate[] = "Network.DnsProxy.$1Query.Errors";
constexpr char kHttpErrors[] = "Network.DnsProxy.DnsOverHttpsQuery.HttpErrors";

constexpr char kQueryDurationTemplate[] = "Network.DnsProxy.Query.$1$2Duration";
constexpr char kQueryDurationResolveTemplate[] =
    "Network.DnsProxy.$1Query.$2ResolveDuration";
constexpr char kQueryDurationReceive[] = "Receive";
constexpr char kQueryDurationReply[] = "Reply";
constexpr char kQueryDurationTotal[] = "Total";
constexpr char kQueryDurationFailed[] = "Failed";
constexpr int kQueryDurationMillisecondsMax = 60 * 1000;
constexpr int kQueryDurationMillisecondsBuckets = 60;

const char* ProcessTypeString(Metrics::ProcessType type) {
  static const std::map<Metrics::ProcessType, const char*> m{
      {Metrics::ProcessType::kController, "Controller"},
      {Metrics::ProcessType::kProxySystem, "SystemProxy"},
      {Metrics::ProcessType::kProxyDefault, "DefaultProxy"},
      {Metrics::ProcessType::kProxyARC, "ARCProxy"},
  };
  const auto it = m.find(type);
  if (it != m.end())
    return it->second;

  return nullptr;
}

const char* QueryTypeString(Metrics::QueryType type) {
  switch (type) {
    case Metrics::QueryType::kPlainText:
      return "PlainText";
    case Metrics::QueryType::kDnsOverHttps:
      return "DnsOverHttps";
    default:
      return nullptr;
  }
}

Metrics::HttpError HttpStatusToError(int status) {
  if (status < 300)
    return Metrics::HttpError::kNone;

  if (status < 400)
    return Metrics::HttpError::kAnyRedirect;

  switch (status) {
    case 400:
      return Metrics::HttpError::kBadRequest;
    case 413:
      return Metrics::HttpError::kPayloadTooLarge;
    case 414:
      return Metrics::HttpError::kURITooLong;
    case 415:
      return Metrics::HttpError::kUnsupportedMediaType;
    case 429:
      return Metrics::HttpError::kTooManyRequests;
    case 501:
      return Metrics::HttpError::kNotImplemented;
    case 502:
      return Metrics::HttpError::kBadGateway;
    default:
      return (status < 500) ? Metrics::HttpError::kOtherClientError
                            : Metrics::HttpError::kOtherServerError;
  }
}

template <typename T>
constexpr auto value_of(T t) {
  return static_cast<std::underlying_type_t<T>>(t);
}

}  // namespace

void Metrics::RecordProcessEvent(Metrics::ProcessType type,
                                 Metrics::ProcessEvent event) {
  if (const char* ts = ProcessTypeString(type)) {
    const auto name =
        base::ReplaceStringPlaceholders(kEventTemplate, {ts}, nullptr);
    metrics_.SendEnumToUMA(name, event);
    return;
  }

  LOG(DFATAL) << "Unknown type: " << value_of(type);
}

void Metrics::RecordNameservers(unsigned int num_ipv4, unsigned int num_ipv6) {
  auto name = base::ReplaceStringPlaceholders(kNameserversCountTemplate,
                                              {kIPv4}, nullptr);
  metrics_.SendToUMA(name, num_ipv4, 1, kNameserversCountMax,
                     kNameserversCountBuckets);

  name = base::ReplaceStringPlaceholders(kNameserversCountTemplate, {kIPv6},
                                         nullptr);
  metrics_.SendToUMA(name, num_ipv6, 1, kNameserversCountMax,
                     kNameserversCountBuckets);

  Metrics::NameserverType ns_type = Metrics::NameserverType::kNone;
  const auto total = num_ipv4 + num_ipv6;
  if (total == num_ipv4)
    ns_type = Metrics::NameserverType::kIPv4;
  else if (total == num_ipv6)
    ns_type = Metrics::NameserverType::kIPv6;
  else if (total != 0)
    ns_type = Metrics::NameserverType::kBoth;

  metrics_.SendEnumToUMA(kNameserverTypes, ns_type);
}

void Metrics::RecordDnsOverHttpsMode(Metrics::DnsOverHttpsMode mode) {
  metrics_.SendEnumToUMA(kDnsOverHttpsMode, mode);
}

void Metrics::RecordQueryResult(Metrics::QueryType type,
                                Metrics::QueryError error,
                                int http_code) {
  const char* qs = QueryTypeString(type);
  if (!qs)
    return;

  auto name =
      base::ReplaceStringPlaceholders(kQueryResultsTemplate, {qs}, nullptr);

  if (error == Metrics::QueryError::kNone) {
    metrics_.SendEnumToUMA(name, Metrics::QueryResult::kSuccess);
    return;
  }
  metrics_.SendEnumToUMA(name, Metrics::QueryResult::kFailure);

  name = base::ReplaceStringPlaceholders(kQueryErrorsTemplate, {qs}, nullptr);
  metrics_.SendEnumToUMA(name, error);

  if (http_code >= 300) {
    metrics_.SendEnumToUMA(kHttpErrors, HttpStatusToError(http_code));
  }
}

void Metrics::RecordQueryDuration(const char* stage, int64_t ms, bool success) {
  const char* prefix = !success ? kQueryDurationFailed : "";
  auto name = base::ReplaceStringPlaceholders(kQueryDurationTemplate,
                                              {prefix, stage}, nullptr);
  metrics_.SendToUMA(name, ms, 1, kQueryDurationMillisecondsMax,
                     kQueryDurationMillisecondsBuckets);
}

void Metrics::RecordQueryResolveDuration(QueryType type,
                                         int64_t ms,
                                         bool success) {
  const char* qs = QueryTypeString(type);
  if (!qs)
    return;

  const char* prefix = !success ? kQueryDurationFailed : "";
  auto name = base::ReplaceStringPlaceholders(kQueryDurationResolveTemplate,
                                              {qs, prefix}, nullptr);
  metrics_.SendToUMA(name, ms, 1, kQueryDurationMillisecondsMax,
                     kQueryDurationMillisecondsBuckets);
}

Metrics::QueryTimer::~QueryTimer() {
  Stop();
  Record(metrics_);
}

void Metrics::QueryTimer::StartReceive() {
  timer_.Start();
}

void Metrics::QueryTimer::StopReceive(bool success) {
  timer_.GetElapsedTime(&elapsed_recv_.second);
  elapsed_recv_.first = success;
  // Timer is stopped here since no further measurable processing will follow.
  if (!success)
    Stop();
}

void Metrics::QueryTimer::StartResolve(bool is_doh) {
  resolv_t_ r;
  r.type = is_doh ? Metrics::QueryType::kDnsOverHttps
                  : Metrics::QueryType::kPlainText;
  timer_.GetElapsedTime(&r.elapsed);
  elapsed_resolve_.emplace_back(r);
}

void Metrics::QueryTimer::StopResolve(bool success) {
  // For unit tests.
  if (elapsed_resolve_.empty())
    return;

  base::TimeDelta d;
  timer_.GetElapsedTime(&d);
  auto& r = elapsed_resolve_.back();
  r.success = success;
  r.elapsed = d - r.elapsed;
}

void Metrics::QueryTimer::StartReply() {
  elapsed_reply_.first = true;
  timer_.GetElapsedTime(&elapsed_reply_.second);
}

void Metrics::QueryTimer::StopReply(bool success) {
  Stop();
  elapsed_reply_.first = success;
  elapsed_reply_.second = elapsed_total_ - elapsed_reply_.second;
}

void Metrics::QueryTimer::Stop() {
  if (timer_.HasStarted()) {
    timer_.GetElapsedTime(&elapsed_total_);
    timer_.Stop();
  }
}

void Metrics::QueryTimer::set_metrics(Metrics* metrics) {
  metrics_ = metrics;
}

void Metrics::QueryTimer::Record(Metrics* metrics) {
  if (!metrics)
    return;

  metrics->RecordQueryDuration(kQueryDurationReceive,
                               elapsed_recv_.second.InMilliseconds(),
                               elapsed_recv_.first);
  if (!elapsed_recv_.first)
    return;

  bool overall = false;
  for (const auto& r : elapsed_resolve_) {
    overall |= r.success;
    metrics->RecordQueryResolveDuration(r.type, r.elapsed.InMilliseconds(),
                                        r.success);
  }

  metrics->RecordQueryDuration(kQueryDurationReply,
                               elapsed_reply_.second.InMilliseconds(),
                               elapsed_reply_.first);

  overall &= elapsed_reply_.first;
  metrics->RecordQueryDuration(kQueryDurationTotal,
                               elapsed_total_.InMilliseconds(), overall);
}

}  // namespace dns_proxy

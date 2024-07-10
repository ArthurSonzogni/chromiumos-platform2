// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <sys/socket.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <base/containers/contains.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/rand_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/socket.h>
#include <chromeos/patchpanel/dns/dns_protocol.h>
#include <chromeos/patchpanel/dns/dns_query.h>
#include <chromeos/patchpanel/dns/io_buffer.h>
#include <chromeos/patchpanel/net_util.h>

// Using directive is necessary to have the overloaded function for socket data
// structure available.
using patchpanel::operator<<;

namespace dns_proxy {
namespace {
constexpr uint32_t kMaxClientTcpConn = 16;
// Given multiple DNS and DoH servers, Resolver will query each servers
// concurrently. |kMaxConcurrentQueries| sets the maximum number of servers to
// query concurrently.
constexpr int kMaxConcurrentQueries = 3;
// Retry delays are reduced by at most |kRetryDelayJitterMultiplier| times to
// avoid coordinated spikes. Having the value >= 1 might introduce an undefined
// behavior.
constexpr float kRetryJitterMultiplier = 0.2;

constexpr base::TimeDelta kProbeInitialDelay = base::Seconds(1);
constexpr base::TimeDelta kProbeMaximumDelay = base::Hours(1);
constexpr float kProbeRetryMultiplier = 1.5;

// The size of TCP header representing the length of the DNS query.
constexpr int kDNSTCPHeaderLength = 2;

// DNS query for resolving "www.gstatic.com" in wire-format data used for
// probing. Transaction ID for the query is empty. This is safe because we
// don't care about the resolving result of the query.
constexpr char kDNSQueryGstatic[] =
    "\x00\x00\x01\x20\x00\x01\x00\x00\x00\x00\x00\x01\x03\x77\x77\x77"
    "\x07\x67\x73\x74\x61\x74\x69\x63\x03\x63\x6f\x6d\x00\x00\x01\x00"
    "\x01\x00\x00\x29\x10\x00\x00\x00\x00\x00\x00\x00";

// Get the time to wait until the next probe.
static base::TimeDelta GetTimeUntilProbe(int num_attempts) {
  base::TimeDelta delay = kProbeInitialDelay;
  delay *= pow(kProbeRetryMultiplier, num_attempts);
  delay -= base::RandDouble() * kRetryJitterMultiplier * delay;
  return std::min(delay, kProbeMaximumDelay);
}

Metrics::QueryError AresStatusMetric(int status) {
  switch (status) {
    case ARES_SUCCESS:
      return Metrics::QueryError::kNone;
    case ARES_ENODATA:
      return Metrics::QueryError::kNoData;
    case ARES_ENOTFOUND:
      return Metrics::QueryError::kDomainNotFound;
    case ARES_ENOTIMP:
      return Metrics::QueryError::kNotImplemented;
    case ARES_EREFUSED:
      return Metrics::QueryError::kQueryRefused;
    case ARES_EFORMERR:
    case ARES_EBADQUERY:
    case ARES_EBADNAME:
    case ARES_EBADFAMILY:
      return Metrics::QueryError::kBadQuery;
    case ARES_ESERVFAIL:
    case ARES_EBADRESP:
      return Metrics::QueryError::kOtherServerError;
    case ARES_ECONNREFUSED:
      return Metrics::QueryError::kConnectionRefused;
    case ARES_ETIMEOUT:
      return Metrics::QueryError::kQueryTimeout;
    default:
      return Metrics::QueryError::kOtherClientError;
  }
}

Metrics::QueryError CurlCodeMetric(int code) {
  switch (code) {
    case CURLE_OK:
      return Metrics::QueryError::kNone;
    case CURLE_UNSUPPORTED_PROTOCOL:
      return Metrics::QueryError::kUnsupportedProtocol;
    case CURLE_URL_MALFORMAT:
    case CURLE_BAD_CONTENT_ENCODING:
      return Metrics::QueryError::kBadQuery;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
      return Metrics::QueryError::kBadHost;
    case CURLE_COULDNT_CONNECT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
      return Metrics::QueryError::kConnectionFailed;
    case CURLE_REMOTE_ACCESS_DENIED:
    case CURLE_SSL_CLIENTCERT:
      return Metrics::QueryError::kConnectionRefused;
    case CURLE_OPERATION_TIMEDOUT:
      return Metrics::QueryError::kQueryTimeout;
    case CURLE_TOO_MANY_REDIRECTS:
      return Metrics::QueryError::kTooManyRedirects;
    case CURLE_GOT_NOTHING:
      return Metrics::QueryError::kNoData;
    case CURLE_SEND_ERROR:
    case CURLE_WRITE_ERROR:
    case CURLE_AGAIN:
      return Metrics::QueryError::kSendError;
    case CURLE_RECV_ERROR:
    case CURLE_READ_ERROR:
      return Metrics::QueryError::kReceiveError;
    case CURLE_WEIRD_SERVER_REPLY:
    case CURLE_RANGE_ERROR:
      return Metrics::QueryError::kOtherServerError;
    default:
      return Metrics::QueryError::kOtherClientError;
  }
}

// Return the next ID for SocketFds.
int NextId() {
  static int next_id = 1;
  return next_id++;
}

}  // namespace

std::ostream& operator<<(std::ostream& stream, const Resolver& resolver) {
  resolver.logger_.Run(stream);
  return stream;
}

Resolver::SocketFd::SocketFd(int type, int fd, size_t buf_size)
    : type(type),
      fd(fd),
      len(0),
      num_retries(0),
      num_active_queries(0),
      bypass_doh(false),
      id(NextId()) {
  buf.resize(buf_size > kMaxDNSBufSize ? kMaxDNSBufSize : buf_size);
  msg = buf.data();
  if (type == SOCK_STREAM) {
    msg += kTCPBufferPaddingLength;
    socklen = 0;
    return;
  }
  socklen = sizeof(src);
}

size_t Resolver::SocketFd::try_resize() {
  size_t remaining_len = buf.size() - len;
  if (type == SOCK_STREAM) {
    remaining_len -= kTCPBufferPaddingLength;
  }

  // Resize only if buffer is full.
  if (remaining_len > 0) {
    return buf.size();
  }
  buf.resize(std::min(2 * buf.size(), kMaxDNSBufSize));

  // Reset |msg| pointer in case of re-allocation.
  msg = buf.data();
  if (type == SOCK_STREAM) {
    msg += kTCPBufferPaddingLength;
  }

  return buf.size();
}

const char* Resolver::SocketFd::get_message() const {
  if (type == SOCK_STREAM) {
    return msg + kDNSTCPHeaderLength;
  }
  return msg;
}

const size_t Resolver::SocketFd::get_length() const {
  if (type == SOCK_STREAM) {
    return len - kDNSTCPHeaderLength;
  }
  return len;
}

Resolver::TCPConnection::TCPConnection(
    std::unique_ptr<net_base::Socket> sock,
    const base::RepeatingCallback<void(int, int)>& callback)
    : sock(std::move(sock)) {
  watcher = base::FileDescriptorWatcher::WatchReadable(
      TCPConnection::sock->Get(),
      base::BindRepeating(callback, TCPConnection::sock->Get(), SOCK_STREAM));
}

Resolver::ProbeState::ProbeState(const std::string& target,
                                 bool doh,
                                 bool validated)
    : target(target), doh(doh), validated(validated), num_retries(0) {}

Resolver::Resolver(base::RepeatingCallback<void(std::ostream& stream)> logger,
                   base::TimeDelta timeout,
                   base::TimeDelta retry_delay,
                   int max_num_retries)
    : logger_(logger),
      always_on_doh_(false),
      doh_enabled_(false),
      retry_delay_(retry_delay),
      max_num_retries_(max_num_retries),
      metrics_(new Metrics) {
  ares_client_ = std::make_unique<AresClient>(timeout);
  curl_client_ = std::make_unique<DoHCurlClient>(timeout);
}

Resolver::Resolver(std::unique_ptr<AresClient> ares_client,
                   std::unique_ptr<DoHCurlClientInterface> curl_client,
                   std::unique_ptr<net_base::SocketFactory> socket_factory,
                   bool disable_probe,
                   std::unique_ptr<Metrics> metrics)
    : logger_(base::DoNothing()),
      socket_factory_(std::move(socket_factory)),
      always_on_doh_(false),
      doh_enabled_(false),
      disable_probe_(disable_probe),
      metrics_(std::move(metrics)),
      ares_client_(std::move(ares_client)),
      curl_client_(std::move(curl_client)) {}

bool Resolver::ListenTCP(struct sockaddr* addr) {
  std::unique_ptr<net_base::Socket> tcp_src =
      socket_factory_->Create(addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK);
  if (!tcp_src) {
    PLOG(ERROR) << *this << " Failed to create TCP socket";
    return false;
  }

  socklen_t len =
      addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  if (!tcp_src->Bind(addr, len)) {
    PLOG(ERROR) << *this << " Cannot bind TCP listening socket to " << *addr;
    return false;
  }

  if (!tcp_src->Listen(kMaxClientTcpConn)) {
    PLOG(ERROR) << *this << " Cannot listen on " << *addr;
    return false;
  }

  // Run the accept loop.
  LOG(INFO) << *this << " Accepting TCP connections on " << *addr;
  tcp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      tcp_src->Get(), base::BindRepeating(&Resolver::OnTCPConnection,
                                          weak_factory_.GetWeakPtr()));
  tcp_src_ = std::move(tcp_src);

  return true;
}

bool Resolver::ListenUDP(struct sockaddr* addr) {
  std::unique_ptr<net_base::Socket> udp_src =
      socket_factory_->Create(addr->sa_family, SOCK_DGRAM | SOCK_NONBLOCK);
  if (!udp_src) {
    PLOG(ERROR) << *this << " Failed to create UDP socket";
    return false;
  }

  socklen_t len =
      addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  if (!udp_src->Bind(addr, len)) {
    PLOG(ERROR) << *this << " Cannot bind UDP socket to " << *addr;
    return false;
  }

  // Start listening.
  LOG(INFO) << *this << " Accepting UDP queries on " << *addr;
  udp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      udp_src->Get(),
      base::BindRepeating(&Resolver::OnDNSQuery, weak_factory_.GetWeakPtr(),
                          udp_src->Get(), SOCK_DGRAM));
  udp_src_ = std::move(udp_src);
  return true;
}

void Resolver::OnTCPConnection() {
  struct sockaddr_storage client_src = {};
  socklen_t sockaddr_len = sizeof(client_src);
  std::unique_ptr<net_base::Socket> client_conn =
      tcp_src_->Accept((struct sockaddr*)&client_src, &sockaddr_len);
  if (!client_conn) {
    PLOG(ERROR) << *this << " Failed to accept TCP connection";
    return;
  }
  tcp_connections_.emplace(
      client_conn->Get(),
      new TCPConnection(std::move(client_conn),
                        base::BindRepeating(&Resolver::OnDNSQuery,
                                            weak_factory_.GetWeakPtr())));
}

bool Resolver::IsNXDOMAIN(const base::span<const unsigned char>& resp) {
  scoped_refptr<patchpanel::WrappedIOBuffer> buf =
      base::MakeRefCounted<patchpanel::WrappedIOBuffer>(
          reinterpret_cast<const char*>(resp.data()));
  auto dns_resp = patchpanel::DnsResponse(buf, resp.size());

  if (!dns_resp.InitParseWithoutQuery(resp.size()) || !dns_resp.IsValid()) {
    LOG(ERROR) << *this << " Failed to parse DNS response";
    return false;
  }
  return dns_resp.rcode() == patchpanel::dns_protocol::kRcodeNXDOMAIN;
}

void Resolver::HandleAresResult(base::WeakPtr<SocketFd> sock_fd,
                                base::WeakPtr<ProbeState> probe_state,
                                int status,
                                const base::span<unsigned char>& resp) {
  // Query is already handled.
  if (!sock_fd) {
    return;
  }

  // Query failed, restart probing.
  // Errors that may be caused by its query's data are not considered as
  // failures:
  // - ARES_FORMERR means that the query data is incorrect.
  // - ARES_ENODATA means that the domain has no answers.
  // - ARES_ENOTIMP means that the operation requested is not implemented.
  // We don't treat this as an error as the user can create these packets
  // manually.
  static const std::set<int> query_success_statuses = {
      ARES_SUCCESS, ARES_EFORMERR, ARES_ENODATA, ARES_ENOTIMP};
  if (probe_state && probe_state->validated &&
      !base::Contains(query_success_statuses, status)) {
    auto target = probe_state->target;
    // |probe_state| will be invalidated by RestartProbe.
    RestartProbe(probe_state);
    int attempt = sock_fd->num_retries + 1;
    LOG(ERROR) << *this << " Do53 query to " << target << " failed after "
               << attempt << " attempt: " << ares_strerror(status) << ". "
               << validated_name_servers_.size() << "/" << name_servers_.size()
               << " validated name servers";
  }

  sock_fd->num_active_queries--;
  // Don't process failing result (including NXDOMAINs) that is not the last
  // result.
  if (sock_fd->num_active_queries > 0 &&
      (status != ARES_SUCCESS || IsNXDOMAIN(resp))) {
    return;
  }

  sock_fd->timer.StopResolve(status == ARES_SUCCESS);
  if (metrics_)
    metrics_->RecordQueryResult(Metrics::QueryType::kPlainText,
                                AresStatusMetric(status));

  if (status == ARES_SUCCESS) {
    ReplyDNS(sock_fd, resp);
    sock_fds_.erase(sock_fd->id);
    return;
  }

  // Process the last unsuccessful result.
  // Retry query upon failure.
  if (sock_fd->num_retries++ >= max_num_retries_) {
    LOG(ERROR) << *this
               << " Failed to do ares lookup: " << ares_strerror(status);
    sock_fds_.erase(sock_fd->id);
    return;
  }

  // Retry resolving the domain.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                                sock_fd, false /* fallback */));
}

void Resolver::HandleCurlResult(base::WeakPtr<SocketFd> sock_fd,
                                base::WeakPtr<ProbeState> probe_state,
                                const DoHCurlClient::CurlResult& res,
                                const base::span<unsigned char>& resp) {
  // Query is already handled.
  if (!sock_fd) {
    return;
  }

  // Query failed, restart probing.
  if (probe_state && probe_state->validated && res.http_code != kHTTPOk) {
    auto target = probe_state->target;
    // |probe_state| will be invalidated by RestartProbe.
    RestartProbe(probe_state);
    int attempt = sock_fd->num_retries + 1;
    LOG(WARNING) << *this << " DoH query to " << target << " failed after "
                 << attempt << " attempt, http status code: " << res.http_code
                 << ". " << validated_doh_providers_.size() << "/"
                 << doh_providers_.size() << " validated DoH providers";
  }

  sock_fd->num_active_queries--;
  // Don't process failing result (including NXDOMAINs) that is not the last
  // result. Check for NXDOMAIN only if the response is valid.
  bool is_nxdomain = false;
  if (res.http_code == kHTTPOk) {
    is_nxdomain = IsNXDOMAIN(resp);
  }
  if ((res.http_code != kHTTPOk || is_nxdomain) &&
      sock_fd->num_active_queries > 0) {
    return;
  }

  sock_fd->timer.StopResolve(res.curl_code == CURLE_OK);
  if (metrics_)
    metrics_->RecordQueryResult(Metrics::QueryType::kDnsOverHttps,
                                CurlCodeMetric(res.curl_code), res.http_code);

  // Process result.
  if (res.curl_code != CURLE_OK) {
    LOG(ERROR) << *this << " DoH resolution failed: "
               << curl_easy_strerror(res.curl_code);
    if (always_on_doh_) {
      // TODO(jasongustaman): Send failure reply with RCODE.
      sock_fds_.erase(sock_fd->id);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(), sock_fd,
                       true /* fallback */));
    return;
  }
  // Retry with plain-text DNS on NXDOMAINs.
  if (is_nxdomain && !always_on_doh_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(), sock_fd,
                       true /* fallback */));
    return;
  }

  switch (res.http_code) {
    case kHTTPOk: {
      ReplyDNS(sock_fd, resp);
      sock_fds_.erase(sock_fd->id);
      return;
    }
    case kHTTPTooManyRequests: {
      if (sock_fd->num_retries >= max_num_retries_) {
        LOG(ERROR) << *this << " Failed to resolve hostname, retried "
                   << max_num_retries_ << " tries";
        sock_fds_.erase(sock_fd->id);
        return;
      }

      // Add jitter to avoid coordinated spikes of retries.
      base::TimeDelta retry_delay_jitter =
          (1 - (base::RandDouble() * kRetryJitterMultiplier)) * retry_delay_;

      // Retry resolving the domain.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                         sock_fd, false /* fallback */),
          retry_delay_jitter);
      sock_fd->num_retries++;
      return;
    }
    default: {
      LOG(ERROR) << *this << " Failed to do curl lookup, HTTP status code: "
                 << res.http_code;
      if (always_on_doh_) {
        // TODO(jasongustaman): Send failure reply with RCODE.
        sock_fds_.erase(sock_fd->id);
        return;
      }
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                         sock_fd, true /* fallback */));
    }
  }
}

void Resolver::HandleDoHProbeResult(base::WeakPtr<ProbeState> probe_state,
                                    const ProbeData& probe_data,
                                    const DoHCurlClient::CurlResult& res,
                                    const base::span<unsigned char>& resp) {
  if (!probe_state) {
    return;
  }

  int attempt = probe_data.num_retries + 1;
  auto now = base::Time::Now();
  auto attempt_latency = now - probe_data.start_time;

  if (res.curl_code != CURLE_OK) {
    LOG(INFO) << *this << " DoH probe attempt " << attempt << " to "
              << probe_state->target
              << " failed: " << curl_easy_strerror(res.curl_code) << " ("
              << attempt_latency << ")";
    return;
  }
  if (res.http_code != kHTTPOk) {
    LOG(INFO) << *this << " DoH probe attempt " << attempt << " to "
              << probe_state->target
              << " failed, HTTP status code: " << res.http_code << " ("
              << attempt_latency << ")";
    return;
  }

  validated_doh_providers_.push_back(probe_state->target);

  LOG(INFO) << *this << " DoH probe attempt " << attempt << " to "
            << probe_state->target << " succeeded (" << attempt_latency << "). "
            << validated_doh_providers_.size() << "/" << doh_providers_.size()
            << " validated DoH providers";

  // Clear the old probe state to stop running probes.
  // Entry must be valid as |probe_state| is still valid.
  const auto& doh_provider = doh_providers_.find(probe_state->target);
  doh_provider->second = std::make_unique<ProbeState>(
      doh_provider->first, probe_state->doh, /*validated=*/true);
}

void Resolver::HandleDo53ProbeResult(base::WeakPtr<ProbeState> probe_state,
                                     const ProbeData& probe_data,
                                     int status,
                                     const base::span<unsigned char>& resp) {
  if (metrics_) {
    metrics_->RecordProbeResult(probe_data.family, probe_data.num_retries,
                                AresStatusMetric(status));
  }
  if (!probe_state) {
    return;
  }

  int attempt = probe_data.num_retries + 1;
  auto now = base::Time::Now();
  auto attempt_latency = now - probe_data.start_time;

  if (status != ARES_SUCCESS) {
    LOG(INFO) << *this << " Do53 probe attempt " << attempt << " to "
              << probe_state->target << " failed: " << ares_strerror(status)
              << " (" << attempt_latency << ")";
    return;
  }

  validated_name_servers_.push_back(probe_state->target);

  LOG(INFO) << *this << " Do53 probe attempt " << attempt << " to "
            << probe_state->target << " succeeded (" << attempt_latency << "). "
            << validated_name_servers_.size() << "/" << name_servers_.size()
            << " validated name servers";

  // Clear the old probe state to stop running probes.
  // Entry must be valid as |probe_state| is still valid.
  const auto& name_server = name_servers_.find(probe_state->target);
  name_server->second = std::make_unique<ProbeState>(
      name_server->first, name_server->second->doh, /*validated=*/true);
}

void Resolver::ReplyDNS(base::WeakPtr<SocketFd> sock_fd,
                        const base::span<unsigned char>& resp) {
  sock_fd->timer.StartReply();
  // For TCP, DNS messages have an additional 2-bytes header representing
  // the length of the query. Add the additional header for the reply.
  uint16_t dns_len = htons(resp.size());
  struct iovec iov_out[2];
  iov_out[0].iov_base = &dns_len;
  iov_out[0].iov_len = 2;
  // For UDP, skip the additional header. By setting |iov_len| to 0, the
  // additional header |dns_len| will not be sent.
  if (sock_fd->type == SOCK_DGRAM) {
    iov_out[0].iov_len = 0;
  }
  iov_out[1].iov_base = static_cast<void*>(resp.data());
  iov_out[1].iov_len = resp.size();
  struct msghdr hdr = {
      .msg_name = nullptr,
      .msg_namelen = 0,
      .msg_iov = iov_out,
      .msg_iovlen = 2,
      .msg_control = nullptr,
      .msg_controllen = 0,
  };
  if (sock_fd->type == SOCK_DGRAM) {
    hdr.msg_name = &sock_fd->src;
    hdr.msg_namelen = sock_fd->socklen;
  }
  const bool ok = sendmsg(sock_fd->fd, &hdr, 0) >= 0;
  sock_fd->timer.StopReply(ok);
  if (!ok) {
    PLOG(ERROR) << *this << " sendmsg() " << sock_fd->fd << " failed";
  }
}

void Resolver::SetNameServers(const std::vector<std::string>& name_servers) {
  SetServers(name_servers, /*doh=*/false);
}

void Resolver::SetDoHProviders(const std::vector<std::string>& doh_providers,
                               bool always_on_doh) {
  always_on_doh_ = always_on_doh;
  doh_enabled_ = !doh_providers.empty();

  SetServers(doh_providers, /*doh=*/true);
}

void Resolver::SetServers(const std::vector<std::string>& new_servers,
                          bool doh) {
  auto& servers = doh ? doh_providers_ : name_servers_;
  auto& validated_servers =
      doh ? validated_doh_providers_ : validated_name_servers_;
  const std::set<std::string> new_servers_set(new_servers.begin(),
                                              new_servers.end());
  bool servers_equal = true;

  // Remove any removed servers.
  for (auto it = servers.begin(); it != servers.end();) {
    if (base::Contains(new_servers_set, it->first)) {
      ++it;
      continue;
    }
    it = servers.erase(it);
    servers_equal = false;
  }

  // Remove any removed servers from validated servers.
  for (auto it = validated_servers.begin(); it != validated_servers.end();) {
    if (base::Contains(new_servers_set, *it)) {
      ++it;
      continue;
    }
    it = validated_servers.erase(it);
  }

  // Probe the new servers.
  for (const auto& new_server : new_servers_set) {
    if (base::Contains(servers, new_server)) {
      continue;
    }
    const auto& probe_state =
        servers
            .emplace(new_server, std::make_unique<ProbeState>(new_server, doh))
            .first;
    Probe(probe_state->second->weak_factory.GetWeakPtr());
    servers_equal = false;
  }

  if (servers_equal)
    return;

  if (doh) {
    LOG(INFO) << *this << " DoH providers are updated, "
              << validated_doh_providers_.size() << "/" << doh_providers_.size()
              << " validated DoH providers";
  } else {
    LOG(INFO) << *this << " Name servers are updated, "
              << validated_name_servers_.size() << "/" << name_servers_.size()
              << " validated name servers";
  }
}

void Resolver::SetDomainDoHConfigs(
    const std::vector<std::string>& doh_included_domains,
    const std::vector<std::string>& doh_excluded_domains) {
  domain_doh_configs_.clear();
  domain_suffix_doh_configs_.clear();
  doh_included_domains_set_ = !doh_included_domains.empty();
  doh_excluded_domains_set_ = !doh_excluded_domains.empty();

  // Only try to match domains when any of the list is not empty.
  if (!doh_included_domains_set_ && !doh_excluded_domains_set_) {
    return;
  }

  // Temporarily store the include and exclude domains in a single list.
  std::vector<std::pair<std::string_view, DomainDoHConfig>> domain_doh_configs;
  for (const auto& domain : doh_excluded_domains) {
    domain_doh_configs.push_back(std::pair<std::string_view, DomainDoHConfig>(
        domain, DomainDoHConfig::kExcluded));
  }
  for (const auto& domain : doh_included_domains) {
    domain_doh_configs.push_back(std::pair<std::string_view, DomainDoHConfig>(
        domain, DomainDoHConfig::kIncluded));
  }

  // Separate DoH bypass domains full match and suffix match.
  for (auto& [domain, config] : domain_doh_configs) {
    if (domain.empty()) {
      LOG(WARNING) << "Invalid domain: empty";
      continue;
    }
    if (domain[0] != '*') {
      // Prefer included domains over excluded domains. Included domains must
      // override excluded domains.
      domain_doh_configs_[std::string(domain)] = config;
      continue;
    }
    domain.remove_prefix(1);
    domain_suffix_doh_configs_.push_back(
        std::make_pair(std::string(domain), config));
  }

  // No need to sort if any of the list is empty. Priority matching is only
  // necessary when there are conflicting configurations.
  if (!doh_included_domains_set_ || !doh_excluded_domains_set_) {
    return;
  }

  // Sort DoH bypass domain suffixes such that longer match is prioritized.
  sort(domain_suffix_doh_configs_.begin(), domain_suffix_doh_configs_.end(),
       [](const auto& a, const auto& b) {
         int ndots_a = count(a.first.begin(), a.first.end(), '.');
         int ndots_b = count(b.first.begin(), b.first.end(), '.');
         if (ndots_a != ndots_b) {
           return ndots_a > ndots_b;
         }
         return a.second == DomainDoHConfig::kIncluded &&
                b.second == DomainDoHConfig::kExcluded;
       });
}

std::unique_ptr<Resolver::SocketFd> Resolver::PopPendingSocketFd(int fd) {
  auto it = pending_sock_fds_.find(fd);
  if (it != pending_sock_fds_.end()) {
    auto sock_fd = std::move(it->second);
    pending_sock_fds_.erase(it);
    return sock_fd;
  }
  return {};
}

void Resolver::OnDNSQuery(int fd, int type) {
  auto sock_fd = PopPendingSocketFd(fd);
  if (!sock_fd) {
    // Initialize SocketFd to carry necessary data.
    sock_fd = std::make_unique<SocketFd>(type, fd);
    // Metrics will be recorded automatically when this object is deleted.
    sock_fd->timer.set_metrics(metrics_.get());
  }

  // If the current buffer is full, resize the container.
  ssize_t buf_size = sock_fd->try_resize();

  // For TCP, it is possible for the packets to be chunked. Move the buffer
  // to the last empty position and adjust the size.
  // For UDP, |sock_fd->len| is always initialized to 0.
  char* buf = sock_fd->buf.data() + sock_fd->len;
  buf_size -= sock_fd->len;

  // For TCP, DNS has an additional 2-bytes header representing the length
  // of the query. Move the receiving buffer, so it is 4-bytes aligned.
  if (sock_fd->type == SOCK_STREAM) {
    buf += kTCPBufferPaddingLength;
    buf_size -= kTCPBufferPaddingLength;
  }

  // Only the last recvfrom call is considered for receive metrics.
  sock_fd->timer.StartReceive();
  ssize_t len = Receive(fd, buf, buf_size,
                        reinterpret_cast<struct sockaddr*>(&sock_fd->src),
                        &sock_fd->socklen);
  // Assume success - on failure, the correct value will be recorded.
  sock_fd->timer.StopReceive(true);
  if (len < 0) {
    sock_fd->timer.StopReceive(false);
    PLOG(WARNING) << *this << " recvfrom failed";
    return;
  }
  // Handle TCP connection closed.
  if (len == 0) {
    sock_fd->timer.StopReceive(false);
    tcp_connections_.erase(fd);
    return;
  }
  sock_fd->len += len;

  HandleDNSQuery(std::move(sock_fd));
}

void Resolver::HandleDNSQuery(std::unique_ptr<SocketFd> sock_fd) {
  // Handle DNS query over UDP.
  if (sock_fd->type == SOCK_DGRAM) {
    CommitQuery(std::move(sock_fd));
    return;
  }

  // Handle DNS query over TCP.
  while (sock_fd) {
    if (sock_fd->len < kDNSTCPHeaderLength) {
      pending_sock_fds_.emplace(sock_fd->fd, std::move(sock_fd));
      return;
    }

    // Check if the current buffer contains a complete DNS query noted by the
    // length taken from the TCP header.
    uint16_t dns_len = ntohs(*reinterpret_cast<uint16_t*>(sock_fd->msg));
    if (sock_fd->len < kDNSTCPHeaderLength + dns_len) {
      pending_sock_fds_.emplace(sock_fd->fd, std::move(sock_fd));
      return;
    }

    // Check if the current buffer contains extra data.
    // Move any remaining data to a new SocketFd.
    std::unique_ptr<SocketFd> tmp_sock_fd = {};
    ssize_t tcp_dns_len = kDNSTCPHeaderLength + dns_len;
    if (sock_fd->len > tcp_dns_len) {
      tmp_sock_fd = std::make_unique<SocketFd>(sock_fd->type, sock_fd->fd,
                                               sock_fd->buf.size());
      tmp_sock_fd->len = sock_fd->len - tcp_dns_len;
      // This copy is safe because the buffer size of |tmp_sock_fd| and
      // |sock_fd| is equal and only partial data of |sock_fd| is copied.
      memcpy(tmp_sock_fd->msg, sock_fd->msg + tcp_dns_len, tmp_sock_fd->len);
      sock_fd->len = tcp_dns_len;
    }

    // Start the DNS query for a complete DNS query data.
    CommitQuery(std::move(sock_fd));

    // Process additional query data.
    sock_fd = std::move(tmp_sock_fd);
  }
}

void Resolver::CommitQuery(std::unique_ptr<SocketFd> sock_fd) {
  if (doh_included_domains_set_ || doh_excluded_domains_set_) {
    base::span<const uint8_t> query(
        reinterpret_cast<const uint8_t*>(sock_fd->get_message()),
        sock_fd->get_length());
    std::optional<std::string> domain = GetDNSQuestionName(query);
    if (domain) {
      sock_fd->bypass_doh = BypassDoH(*domain);
    } else {
      LOG(WARNING) << "Failed to get DNS question name";
    }
  }

  const auto& sock_fd_it =
      sock_fds_.emplace(sock_fd->id, std::move(sock_fd)).first;
  Resolve(sock_fd_it->second->weak_factory.GetWeakPtr());
}

bool Resolver::ResolveDNS(base::WeakPtr<SocketFd> sock_fd, bool doh) {
  if (!sock_fd) {
    LOG(ERROR) << *this
               << " Unexpected ResolveDNS() call with deleted SocketFd";
    return false;
  }

  const auto query_type =
      doh ? Metrics::QueryType::kDnsOverHttps : Metrics::QueryType::kPlainText;
  const auto& name_servers = GetActiveNameServers();
  if (name_servers.empty()) {
    LOG(ERROR) << *this << " Name server list must not be empty";
    if (metrics_) {
      metrics_->RecordQueryResult(query_type,
                                  Metrics::QueryError::kEmptyNameServers);
    }
    return false;
  }

  const auto& doh_providers = GetActiveDoHProviders();
  if (doh && doh_providers.empty()) {
    // No DoH providers are currently validated, fallback to Do53.
    if (!doh_providers_.empty()) {
      return false;
    }
    LOG(ERROR) << *this << " DoH provider list must not be empty";
    if (metrics_) {
      metrics_->RecordQueryResult(Metrics::QueryType::kDnsOverHttps,
                                  Metrics::QueryError::kEmptyDoHProviders);
    }
    return false;
  }

  // Start multiple concurrent queries.
  const auto& targets = doh ? doh_providers : name_servers;
  for (const auto& target : targets) {
    if (doh) {
      if (!curl_client_->Resolve(
              base::span<const char>(sock_fd->get_message(),
                                     sock_fd->get_length()),
              base::BindRepeating(
                  &Resolver::HandleCurlResult, weak_factory_.GetWeakPtr(),
                  sock_fd, doh_providers_[target]->weak_factory.GetWeakPtr()),
              name_servers, target)) {
        continue;
      }
    } else {
      if (!ares_client_->Resolve(
              base::span<const unsigned char>(
                  reinterpret_cast<const unsigned char*>(
                      sock_fd->get_message()),
                  sock_fd->get_length()),
              base::BindRepeating(
                  &Resolver::HandleAresResult, weak_factory_.GetWeakPtr(),
                  sock_fd, name_servers_[target]->weak_factory.GetWeakPtr()),
              target, sock_fd->type)) {
        continue;
      }
    }
    if (++sock_fd->num_active_queries >= kMaxConcurrentQueries) {
      break;
    }
  }

  if (sock_fd->num_active_queries > 0)
    return true;

  LOG(ERROR) << *this << " No requests successfully started for query";
  if (metrics_) {
    metrics_->RecordQueryResult(
        query_type, Metrics::QueryError::kClientInitializationError);
  }
  return false;
}

std::vector<std::string> Resolver::GetActiveDoHProviders() {
  if (!always_on_doh_ || !validated_doh_providers_.empty())
    return validated_doh_providers_;

  std::vector<std::string> doh_providers;
  for (const auto& doh_provider : doh_providers_) {
    doh_providers.push_back(doh_provider.first);
  }
  return doh_providers;
}

std::vector<std::string> Resolver::GetActiveNameServers() {
  if (!validated_name_servers_.empty())
    return validated_name_servers_;

  std::vector<std::string> name_servers;
  for (const auto& name_server : name_servers_) {
    name_servers.push_back(name_server.first);
  }
  return name_servers;
}

void Resolver::RestartProbe(base::WeakPtr<ProbeState> probe_state) {
  if (!probe_state)
    return;

  auto& targets = probe_state->doh ? doh_providers_ : name_servers_;
  auto& validated_targets =
      probe_state->doh ? validated_doh_providers_ : validated_name_servers_;
  validated_targets.erase(
      std::remove(validated_targets.begin(), validated_targets.end(),
                  probe_state->target),
      validated_targets.end());

  const auto& target = targets.find(probe_state->target);
  target->second =
      std::make_unique<ProbeState>(target->first, probe_state->doh);
  Probe(target->second->weak_factory.GetWeakPtr());
}

void Resolver::Probe(base::WeakPtr<ProbeState> probe_state) {
  if (disable_probe_)
    return;

  if (!probe_state)
    return;

  // Schedule the next probe now as the probe may run for a long time.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Resolver::Probe, weak_factory_.GetWeakPtr(), probe_state),
      GetTimeUntilProbe(probe_state->num_retries));

  // Run the probe.
  const auto target_ip =
      net_base::IPAddress::CreateFromString(probe_state->target);
  const sa_family_t target_family =
      target_ip ? net_base::ToSAFamily(target_ip->GetFamily()) : AF_UNSPEC;
  const ProbeData probe_data = {target_family, probe_state->num_retries,
                                base::Time::Now()};
  if (probe_state->doh) {
    curl_client_->Resolve(
        base::span<const char>(kDNSQueryGstatic, sizeof(kDNSQueryGstatic)),
        base::BindRepeating(&Resolver::HandleDoHProbeResult,
                            weak_factory_.GetWeakPtr(), probe_state,
                            probe_data),
        GetActiveNameServers(), probe_state->target);
  } else {
    ares_client_->Resolve(
        base::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(kDNSQueryGstatic),
            sizeof(kDNSQueryGstatic)),
        base::BindRepeating(&Resolver::HandleDo53ProbeResult,
                            weak_factory_.GetWeakPtr(), probe_state,
                            probe_data),
        probe_state->target);
  }
  probe_state->num_retries++;
}

void Resolver::Resolve(base::WeakPtr<SocketFd> sock_fd, bool fallback) {
  if (!sock_fd) {
    LOG(ERROR) << *this << " Unexpected Resolve() call with deleted SocketFd";
    return;
  }

  if (doh_enabled_ && !fallback && !sock_fd->bypass_doh) {
    sock_fd->timer.StartResolve(true);
    if (ResolveDNS(sock_fd, /*doh=*/true))
      return;

    sock_fd->timer.StopResolve(false);
  }
  if (!always_on_doh_ || sock_fd->bypass_doh) {
    sock_fd->timer.StartResolve();
    if (ResolveDNS(sock_fd, /*doh=*/false))
      return;

    sock_fd->timer.StopResolve(false);
  }

  // Construct and send a response indicating that there is a failure.
  patchpanel::DnsResponse response = ConstructServFailResponse(
      base::span<const char>(sock_fd->get_message(), sock_fd->get_length()));
  ReplyDNS(sock_fd, base::span<unsigned char>(reinterpret_cast<unsigned char*>(
                                                  response.io_buffer()->data()),
                                              response.io_buffer_size()));

  // Query is completed, remove SocketFd.
  sock_fds_.erase(sock_fd->id);
}

bool Resolver::BypassDoH(const std::string& domain) {
  auto domain_doh_config = GetDomainDoHConfig(domain);
  // If the domain in the query does not match any entry, check the DoH included
  // domains list. If set, default to bypass DoH. Otherwise, always prefer to
  // use DoH.
  if (!domain_doh_config) {
    return doh_included_domains_set_;
  }
  return domain_doh_config == DomainDoHConfig::kExcluded;
}

std::optional<Resolver::DomainDoHConfig> Resolver::GetDomainDoHConfig(
    const std::string& domain) {
  // Compare |domain| with the map of FQDNs.
  auto it = domain_doh_configs_.find(domain);
  if (it != domain_doh_configs_.end()) {
    return it->second;
  }
  // Compare |domain| with the list of domain suffixes.
  for (const auto& [suffix, config] : domain_suffix_doh_configs_) {
    if (domain.ends_with(suffix)) {
      return config;
    }
  }
  // Domain does not match any configuration.
  return std::nullopt;
}

std::optional<std::string> Resolver::GetDNSQuestionName(
    const base::span<const uint8_t>& query) {
  // Index of a DNS query question name field. This is taken from RFC1035 (DNS).
  // The first 12 bytes contains the header of the query.
  static constexpr size_t kQNameIdx = 12;
  if (query.size() <= kQNameIdx) {
    return std::nullopt;
  }
  auto reader = base::SpanReader(query);
  reader.Skip(kQNameIdx);

  std::string qname;
  qname.reserve(patchpanel::dns_protocol::kMaxNameLength);

  // From RFC1035 (DNS), section 4.1.2. question section format:
  // A domain name is represented as a sequence of labels, where each label
  // consists of a length octet followed by that number of octets. The domain
  // name terminates with the zero length octet for the null label of the root.
  // As an example, "google.com" is represented as "\x06google\x03com\x00".
  uint8_t label_length;
  if (!reader.ReadU8BigEndian(label_length)) {
    return std::nullopt;
  }
  while (label_length) {
    base::span<const uint8_t> label;
    // Misformatted query, the length octet must be followed by label with the
    // same number of octets. Query is cut short.
    if (!reader.ReadInto(label_length, label)) {
      return std::nullopt;
    }

    // Validate and append characters in the domain.
    for (const char c : label) {
      if (!base::IsAsciiAlpha(c) && !base::IsAsciiDigit(c) && c != '-') {
        return std::nullopt;
      }
      qname.append(1, c);
    }

    // Append dots ('.') if there is a following label.
    if (!reader.ReadU8BigEndian(label_length)) {
      return std::nullopt;
    }
    if (label_length) {
      qname.append(1, '.');
    }

    // Validate the domain length.
    if (qname.size() > patchpanel::dns_protocol::kMaxNameLength) {
      return std::nullopt;
    }
  }

  qname.shrink_to_fit();
  return qname;
}

patchpanel::DnsResponse Resolver::ConstructServFailResponse(
    const base::span<const char>& query) {
  // Construct a DNS query from the message buffer.
  std::optional<patchpanel::DnsQuery> dns_query;
  if (query.size() > 0 && query.size() <= dns_proxy::kMaxDNSBufSize) {
    scoped_refptr<patchpanel::IOBufferWithSize> buf =
        base::MakeRefCounted<patchpanel::IOBufferWithSize>(query.size());
    memcpy(buf->data(), query.data(), query.size());
    dns_query = patchpanel::DnsQuery(buf);
  }

  // Set the query id as 0 if the query is invalid.
  uint16_t query_id = 0;
  if (dns_query.has_value() && dns_query->Parse(query.size())) {
    query_id = dns_query->id();
  } else {
    dns_query.reset();
  }

  // Returns RCODE SERVFAIL response corresponding to the query.
  patchpanel::DnsResponse response(query_id, false /* is_authoritative */,
                                   {} /* answers */, {} /* authority_records */,
                                   {} /* additional_records */, dns_query,
                                   patchpanel::dns_protocol::kRcodeSERVFAIL);
  return response;
}

void Resolver::SetProbingEnabled(bool enable_probe) {
  disable_probe_ = !enable_probe;
}

ssize_t Resolver::Receive(int fd,
                          char* buffer,
                          size_t buffer_size,
                          struct sockaddr* src_addr,
                          socklen_t* addrlen) {
  return recvfrom(fd, buffer, buffer_size, 0, src_addr, addrlen);
}
}  // namespace dns_proxy

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <optional>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/rand_util.h>
#include <base/threading/thread_task_runner_handle.h>
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
// Retries are delayed by +/- |kRetryDelayJitterMultiplier| times to avoid
// coordinated spikes.
constexpr float kRetryDelayJitterMultiplier = 0.15;

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

}  // namespace

Resolver::SocketFd::SocketFd(int type, int fd, int num_active_queries)
    : type(type),
      fd(fd),
      num_retries(0),
      num_active_queries(num_active_queries),
      request_handled(false) {
  if (type == SOCK_STREAM) {
    socklen = 0;
    return;
  }
  socklen = sizeof(src);
}

Resolver::TCPConnection::TCPConnection(
    std::unique_ptr<patchpanel::Socket> sock,
    const base::RepeatingCallback<void(int, int)>& callback)
    : sock(std::move(sock)) {
  watcher = base::FileDescriptorWatcher::WatchReadable(
      TCPConnection::sock->fd(),
      base::BindRepeating(callback, TCPConnection::sock->fd(), SOCK_STREAM));
}

Resolver::Resolver(base::TimeDelta timeout,
                   base::TimeDelta retry_delay,
                   int max_num_retries)
    : always_on_doh_(false),
      doh_enabled_(false),
      retry_delay_(retry_delay),
      max_num_retries_(max_num_retries),
      metrics_(new Metrics) {
  ares_client_ = std::make_unique<AresClient>(timeout);
  curl_client_ = std::make_unique<DoHCurlClient>(timeout);
}

Resolver::Resolver(std::unique_ptr<AresClient> ares_client,
                   std::unique_ptr<DoHCurlClientInterface> curl_client,
                   std::unique_ptr<Metrics> metrics)
    : always_on_doh_(false),
      doh_enabled_(false),
      metrics_(std::move(metrics)),
      ares_client_(std::move(ares_client)),
      curl_client_(std::move(curl_client)) {}

bool Resolver::ListenTCP(struct sockaddr* addr) {
  auto tcp_src = std::make_unique<patchpanel::Socket>(
      addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK);

  socklen_t len =
      addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  if (!tcp_src->Bind(addr, len)) {
    LOG(ERROR) << "Cannot bind source socket to " << *addr;
    return false;
  }

  if (!tcp_src->Listen(kMaxClientTcpConn)) {
    LOG(ERROR) << "Cannot listen on " << *addr;
    return false;
  }

  // Run the accept loop.
  LOG(INFO) << "Accepting connections on " << *addr;
  tcp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      tcp_src->fd(), base::BindRepeating(&Resolver::OnTCPConnection,
                                         weak_factory_.GetWeakPtr()));
  tcp_src_ = std::move(tcp_src);

  return true;
}

bool Resolver::ListenUDP(struct sockaddr* addr) {
  auto udp_src = std::make_unique<patchpanel::Socket>(
      addr->sa_family, SOCK_DGRAM | SOCK_NONBLOCK);

  socklen_t len =
      addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  if (!udp_src->Bind(addr, len)) {
    LOG(ERROR) << "Cannot bind source socket to " << *addr;
    return false;
  }

  // Start listening.
  LOG(INFO) << "Accepting connections on " << *addr;
  udp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      udp_src->fd(),
      base::BindRepeating(&Resolver::OnDNSQuery, weak_factory_.GetWeakPtr(),
                          udp_src->fd(), SOCK_DGRAM));
  udp_src_ = std::move(udp_src);
  return true;
}

void Resolver::OnTCPConnection() {
  struct sockaddr_storage client_src = {};
  socklen_t sockaddr_len = sizeof(client_src);
  auto client_conn =
      tcp_src_->Accept((struct sockaddr*)&client_src, &sockaddr_len);
  if (!client_conn) {
    LOG(ERROR) << "Failed to accept TCP connection";
    return;
  }
  tcp_connections_.emplace(
      client_conn->fd(),
      new TCPConnection(std::move(client_conn),
                        base::BindRepeating(&Resolver::OnDNSQuery,
                                            weak_factory_.GetWeakPtr())));
}

void Resolver::HandleAresResult(SocketFd* sock_fd,
                                int status,
                                unsigned char* msg,
                                size_t len) {
  sock_fd->num_active_queries--;
  if (!sock_fd->request_handled &&
      (status == ARES_SUCCESS || sock_fd->num_active_queries == 0)) {
    HandleCombinedAresResult(sock_fd, status, msg, len);
  }

  // Delete |sock_fd| if all queries are processed and retry is not scheduled.
  if (sock_fd->request_handled && sock_fd->num_active_queries == 0) {
    delete sock_fd;
  }
}

void Resolver::HandleCombinedAresResult(SocketFd* sock_fd,
                                        int status,
                                        unsigned char* msg,
                                        size_t len) {
  sock_fd->timer.StopResolve(status == ARES_SUCCESS);
  if (metrics_)
    metrics_->RecordQueryResult(Metrics::QueryType::kPlainText,
                                AresStatusMetric(status));

  if (status == ARES_SUCCESS) {
    sock_fd->request_handled = true;
    ReplyDNS(sock_fd, msg, len);
    return;
  }

  // Retry query upon failure.
  if (sock_fd->num_retries++ >= max_num_retries_) {
    LOG(ERROR) << "Failed to do ares lookup: " << ares_strerror(status);
    sock_fd->request_handled = true;
    return;
  }

  // Retry resolving the domain.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                                sock_fd, false /* fallback */));
  return;
}

void Resolver::HandleCurlResult(SocketFd* sock_fd,
                                const DoHCurlClient::CurlResult& res,
                                unsigned char* msg,
                                size_t len) {
  sock_fd->num_active_queries--;
  if (!sock_fd->request_handled &&
      (res.http_code == kHTTPOk || sock_fd->num_active_queries == 0)) {
    HandleCombinedCurlResult(sock_fd, res, msg, len);
  }

  // Delete |sock_fd| if all queries are processed and retry is not scheduled.
  if (sock_fd->request_handled && sock_fd->num_active_queries == 0) {
    delete sock_fd;
  }
}

void Resolver::HandleCombinedCurlResult(SocketFd* sock_fd,
                                        const DoHCurlClient::CurlResult& res,
                                        unsigned char* msg,
                                        size_t len) {
  sock_fd->timer.StopResolve(res.curl_code == CURLE_OK);
  if (metrics_)
    metrics_->RecordQueryResult(Metrics::QueryType::kDnsOverHttps,
                                CurlCodeMetric(res.curl_code), res.http_code);

  if (res.curl_code != CURLE_OK) {
    LOG(ERROR) << "DoH resolution failed: "
               << curl_easy_strerror(res.curl_code);
    if (always_on_doh_) {
      // TODO(jasongustaman): Send failure reply with RCODE.
      sock_fd->request_handled = true;
      return;
    }
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(), sock_fd,
                       true /* fallback */));
    return;
  }

  switch (res.http_code) {
    case kHTTPOk: {
      ReplyDNS(sock_fd, msg, len);
      sock_fd->request_handled = true;
      return;
    }
    case kHTTPTooManyRequests: {
      if (sock_fd->num_retries >= max_num_retries_) {
        LOG(ERROR) << "Failed to resolve hostname, retried " << max_num_retries_
                   << " tries";
        sock_fd->request_handled = true;
        return;
      }

      // Add jitter to avoid coordinated spikes of retries.
      double rand_multiplier = 1 - base::RandDouble() * 2;
      base::TimeDelta retry_delay_jitter =
          (1 + rand_multiplier * kRetryDelayJitterMultiplier) * retry_delay_;

      // Retry resolving the domain.
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                         sock_fd, false /* fallback */),
          retry_delay_jitter);
      sock_fd->num_retries++;
      return;
    }
    default: {
      LOG(ERROR) << "Failed to do curl lookup, HTTP status code "
                 << res.http_code;
      if (always_on_doh_) {
        // TODO(jasongustaman): Send failure reply with RCODE.
        sock_fd->request_handled = true;
      } else {
        base::ThreadTaskRunnerHandle::Get()->PostTask(
            FROM_HERE,
            base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                           sock_fd, true /* fallback */));
      }
      return;
    }
  }
}

void Resolver::ReplyDNS(SocketFd* sock_fd, unsigned char* msg, size_t len) {
  sock_fd->timer.StartReply();
  // For TCP, DNS messages have an additional 2-bytes header representing
  // the length of the query. Add the additional header for the reply.
  uint16_t dns_len = htons(len);
  struct iovec iov_out[2];
  iov_out[0].iov_base = &dns_len;
  iov_out[0].iov_len = 2;
  // For UDP, skip the additional header. By setting |iov_len| to 0, the
  // additional header |dns_len| will not be sent.
  if (sock_fd->type == SOCK_DGRAM) {
    iov_out[0].iov_len = 0;
  }
  iov_out[1].iov_base = static_cast<void*>(msg);
  iov_out[1].iov_len = len;
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
    PLOG(ERROR) << "sendmsg() " << sock_fd->fd << " failed";
  }
}

void Resolver::SetNameServers(const std::vector<std::string>& name_servers) {
  name_servers_ = name_servers;
}

void Resolver::SetDoHProviders(const std::vector<std::string>& doh_providers,
                               bool always_on_doh) {
  always_on_doh_ = always_on_doh;
  doh_enabled_ = !doh_providers.empty();
  doh_providers_ = doh_providers;
}

void Resolver::OnDNSQuery(int fd, int type) {
  // Initialize SocketFd to carry necessary data. |sock_fd| must be freed when
  // it is done being used.
  SocketFd* sock_fd = new SocketFd(type, fd);
  // Metrics will be recorded automatically when this object is deleted.
  sock_fd->timer.set_metrics(metrics_.get());

  size_t buf_size;
  struct sockaddr* src;
  switch (type) {
    case SOCK_DGRAM:
      sock_fd->msg = sock_fd->buf;
      buf_size = kDNSBufSize;
      src = reinterpret_cast<struct sockaddr*>(&sock_fd->src);
      break;
    case SOCK_STREAM:
      // For TCP, DNS has an additional 2-bytes header representing the length
      // of the query. Move the receiving buffer, so it is 4-bytes aligned.
      sock_fd->msg = sock_fd->buf + 2;
      buf_size = kDNSBufSize - 2;
      src = nullptr;
      break;
    default:
      LOG(DFATAL) << "Unexpected socket type: " << type;
      return;
  }
  sock_fd->timer.StartReceive();
  sock_fd->len =
      recvfrom(fd, sock_fd->msg, buf_size, 0, src, &sock_fd->socklen);
  // Assume success - on failure, the correct value will be recorded.
  sock_fd->timer.StopReceive(true);
  if (sock_fd->len < 0) {
    sock_fd->timer.StopReceive(false);
    PLOG(WARNING) << "recvfrom failed";
    delete sock_fd;
    return;
  }
  // Handle TCP connection closed.
  if (sock_fd->len == 0) {
    sock_fd->timer.StopReceive(false);
    delete sock_fd;
    tcp_connections_.erase(fd);
    return;
  }

  // For TCP, DNS have an additional 2-bytes header representing the length of
  // the query. Trim the additional header to be used by CURL or Ares.
  if (type == SOCK_STREAM && sock_fd->len > 2) {
    sock_fd->msg += 2;
    sock_fd->len -= 2;
  }

  Resolve(sock_fd);
}

bool Resolver::ResolveDNS(SocketFd* sock_fd, bool doh) {
  const auto query_type =
      doh ? Metrics::QueryType::kDnsOverHttps : Metrics::QueryType::kPlainText;
  if (name_servers_.empty()) {
    LOG(ERROR) << "Name server list must not be empty";
    if (metrics_) {
      metrics_->RecordQueryResult(query_type,
                                  Metrics::QueryError::kEmptyNameServers);
    }
    return false;
  }

  if (doh && doh_providers_.empty()) {
    LOG(ERROR) << "DoH provider list must not be empty";
    if (metrics_) {
      metrics_->RecordQueryResult(Metrics::QueryType::kDnsOverHttps,
                                  Metrics::QueryError::kEmptyDoHProviders);
    }
    return false;
  }

  // Start multiple concurrent queries.
  const auto& targets = doh ? doh_providers_ : name_servers_;
  for (const auto& target : targets) {
    if (doh) {
      if (!curl_client_->Resolve(
              sock_fd->msg, sock_fd->len,
              base::BindRepeating(&Resolver::HandleCurlResult,
                                  weak_factory_.GetWeakPtr(), sock_fd),
              name_servers_, target)) {
        continue;
      }
    } else {
      if (!ares_client_->Resolve(
              reinterpret_cast<const unsigned char*>(sock_fd->msg),
              sock_fd->len,
              base::BindRepeating(&Resolver::HandleAresResult,
                                  weak_factory_.GetWeakPtr(), sock_fd),
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

  LOG(ERROR) << "No requests for query";
  if (metrics_) {
    metrics_->RecordQueryResult(
        query_type, Metrics::QueryError::kClientInitializationError);
  }
  return false;
}

void Resolver::Resolve(SocketFd* sock_fd, bool fallback) {
  if (doh_enabled_ && !fallback) {
    sock_fd->timer.StartResolve(true);
    if (ResolveDNS(sock_fd, /*doh=*/true))
      return;

    sock_fd->timer.StopResolve(false);
  }
  if (!always_on_doh_) {
    sock_fd->timer.StartResolve();
    if (ResolveDNS(sock_fd, /*doh=*/false))
      return;

    sock_fd->timer.StopResolve(false);
  }

  // Construct and send a response indicating that there is a failure.
  patchpanel::DnsResponse response =
      ConstructServFailResponse(sock_fd->msg, sock_fd->len);
  ReplyDNS(sock_fd,
           reinterpret_cast<unsigned char*>(response.io_buffer()->data()),
           response.io_buffer_size());
  // |sock_fd| pointer must be deleted when the request associated with the
  // pointer is done. Normally, the pointer is deleted after c-ares or CURL
  // finish handling the request, `HandleAresResult(...)` or
  // `HandleCurlResult(...)`. However, we need to do it here because there is an
  // error when starting the request of c-ares or CURL resulting in no query
  // sent to the name servers, completing the request by sending a failure
  // response.
  delete sock_fd;
}

patchpanel::DnsResponse Resolver::ConstructServFailResponse(const char* msg,
                                                            int len) {
  // Construct a DNS query from the message buffer.
  std::optional<patchpanel::DnsQuery> query;
  if (len > 0 && len <= dns_proxy::kDNSBufSize) {
    scoped_refptr<patchpanel::IOBufferWithSize> query_buf =
        base::MakeRefCounted<patchpanel::IOBufferWithSize>(len);
    memcpy(query_buf->data(), msg, len);
    query = patchpanel::DnsQuery(query_buf);
  }

  // Set the query id as 0 if the query is invalid.
  uint16_t query_id = 0;
  if (query.has_value() && query->Parse(len)) {
    query_id = query->id();
  } else {
    query.reset();
  }

  // Returns RCODE SERVFAIL response corresponding to the query.
  patchpanel::DnsResponse response(query_id, false /* is_authoritative */,
                                   {} /* answers */, {} /* authority_records */,
                                   {} /* additional_records */, query,
                                   patchpanel::dns_protocol::kRcodeSERVFAIL);
  return response;
}
}  // namespace dns_proxy

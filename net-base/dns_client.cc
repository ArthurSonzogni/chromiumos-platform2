// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/dns_client.h"

#include <ares.h>
#include <netdb.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>

#include "net-base/ares_interface.h"
#include "net-base/ip_address.h"

namespace net_base {

namespace {

// Returns the list of IP address from |ares_addrinfo|. Returns empty vector on
// parsing failures.
std::vector<IPAddress> GetIPsFromAddrinfo(IPFamily expected_family,
                                          const struct ares_addrinfo* info) {
  std::vector<IPAddress> ret;
  if (!info) {
    LOG(ERROR) << "info is nullptr";
    return ret;
  }

  for (struct ares_addrinfo_node* node = info->nodes; node != nullptr;
       node = node->ai_next) {
    if (node->ai_family != ToSAFamily(expected_family)) {
      continue;
    }

    if (node->ai_family == AF_INET) {
      ret.emplace_back(IPv4Address(
          reinterpret_cast<struct sockaddr_in*>(node->ai_addr)->sin_addr));
    } else if (node->ai_family == AF_INET6) {
      ret.emplace_back(IPv6Address(
          reinterpret_cast<struct sockaddr_in6*>(node->ai_addr)->sin6_addr));
    }
  }
  return ret;
}

constexpr DNSClient::Error AresStatusToError(int status) {
  using Error = DNSClient::Error;
  switch (status) {
    case ARES_ENODATA:
      return Error::kNoData;
    case ARES_EFORMERR:
      return Error::kFormErr;
    case ARES_ESERVFAIL:
      return Error::kServerFail;
    case ARES_ENOTFOUND:
      return Error::kNotFound;
    case ARES_ENOTIMP:
      return Error::kNotImplemented;
    case ARES_EREFUSED:
      return Error::kRefused;
    case ARES_EBADQUERY:
      return Error::kBadQuery;
    case ARES_EBADNAME:
      return Error::kBadName;
    case ARES_EBADFAMILY:
      return Error::kBadFamily;
    case ARES_EBADRESP:
      return Error::kBadResp;
    case ARES_ECONNREFUSED:
      return Error::kConnRefused;
    case ARES_ETIMEOUT:
      return Error::kTimedOut;
    default:
      LOG(ERROR) << "Unexpected ares status " << status;
      return Error::kInternal;
  }
}

class DNSClientImpl : public DNSClient {
 public:
  DNSClientImpl(IPFamily family,
                std::string_view hostname,
                CallbackWithDuration callback,
                const Options& options,
                AresInterface* ares);
  ~DNSClientImpl() override;

 private:
  // Clean up the internal state.
  void CleanUp();

  // Callback from c-ares.
  static void AresGetaddrinfoCallback(void* arg,
                                      int status,
                                      int timeouts,
                                      struct ares_addrinfo* result);
  void ProcessGetaddrinfoCallback(int status, struct ares_addrinfo* result);

  // Helper functions to invoke the callback. Passing by values are expected
  // here since we need move or copy inside the functions.
  void ReportSuccess(base::TimeTicks stop, std::vector<IPAddress> ip_addrs);
  void ReportFailure(base::TimeTicks stop, Error err);
  void ScheduleStopAndInvokeCallback(base::TimeTicks stop, Result result);
  void StopAndInvokeCallback(base::OnceClosure cb_with_result);

  // Helper functions for the fd management.
  // - OnSocketReadable(), OnSocketWritable(), and OnTimeout(): Event handlers
  //   called on the corresponding events. These three functions will call
  //   ProcessFd() inside.
  // - ProcessFd(): called ares_process_fd(), which may invoke the callback
  //   (i.e., AresGetaddrinfoCallback()).
  // - RefreshHandlers(): Set up the event handlers (OnSocketReadable(),
  //   OnSocketWritable()).
  // - RefreshTimeout(): Set up OnTimeout() and called by OnTimeout(). Note that
  //   the timeout is scheduled at `min(ares_fd_timeout, dns_client_timeout)`.
  //   The former one is the signal that we need to call `ares_process_fd()` to
  //   let it handle the events, while the latter one is the signal that we need
  //   to return the execution of DNSClient. We only need to reset the timeout
  //   in the former case, and we unify the logic here just for simplicity.
  void OnSocketReadable(int fd);
  void OnSocketWritable(int fd);
  void OnTimeout();
  void ProcessFd(int read_fd, int write_fd);
  void RefreshHandlers();
  void RefreshTimeout();

  // Returns true if this object hasn't get the results.
  bool IsRunning() const { return !callback_.is_null(); }

  AresInterface* ares_;

  const IPFamily family_;
  const base::TimeTicks start_;
  const base::TimeTicks deadline_;

  ares_channel channel_ = nullptr;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      read_handlers_;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      write_handlers_;

  CallbackWithDuration callback_;

  // For cancelling the ongoing timeout task.
  base::WeakPtrFactory<DNSClientImpl> weak_factory_for_timeout_{this};
  // The weak pointers created by this weak factory have the same lifetime with
  // the object.
  base::WeakPtrFactory<DNSClientImpl> weak_factory_{this};
};

DNSClientImpl::DNSClientImpl(IPFamily family,
                             std::string_view hostname,
                             CallbackWithDuration callback,
                             const Options& options,
                             AresInterface* ares)
    : ares_(ares),
      family_(family),
      start_(base::TimeTicks::Now()),
      deadline_(start_ + options.timeout),
      callback_(std::move(callback)) {
  struct ares_options ares_opts;
  memset(&ares_opts, 0, sizeof(ares_opts));

  int opt_mask = 0;

  if (options.per_query_initial_timeout.has_value()) {
    auto per_query_timeout = *options.per_query_initial_timeout;
    static constexpr auto kMaxPerQueryInitialTimeout = base::Minutes(1);
    if (per_query_timeout > kMaxPerQueryInitialTimeout) {
      LOG(ERROR) << "Input per query timeout " << per_query_timeout.InSeconds()
                 << "s is too long, reset to max timeout "
                 << kMaxPerQueryInitialTimeout.InSeconds() << "s";
      per_query_timeout = kMaxPerQueryInitialTimeout;
    }

    ares_opts.timeout = static_cast<int>(per_query_timeout.InMilliseconds());
    opt_mask |= ARES_OPT_TIMEOUTMS;
  }

  if (options.number_of_tries.has_value()) {
    ares_opts.tries = *options.number_of_tries;
    opt_mask |= ARES_OPT_TRIES;
  }

  int status = ares_->init_options(&channel_, &ares_opts, opt_mask);
  if (status != ARES_SUCCESS) {
    ReportFailure(start_, AresStatusToError(status));
    return;
  }

  if (!options.interface.empty()) {
    ares_->set_local_dev(channel_, options.interface.c_str());
  }

  if (options.name_server.has_value()) {
    status = ares_->set_servers_csv(channel_,
                                    options.name_server->ToString().c_str());
    if (status != ARES_SUCCESS) {
      ReportFailure(start_, AresStatusToError(status));
      return;
    }
  }

  struct ares_addrinfo_hints hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = ToSAFamily(family_);
  // The raw pointer here is safe since the callback can only be invoked inside
  // some c-ares functions, while they can only be called from this object.
  ares_->getaddrinfo(channel_, std::string(hostname).c_str(), nullptr, &hints,
                     AresGetaddrinfoCallback, this);

  RefreshHandlers();
  RefreshTimeout();
}

DNSClientImpl::~DNSClientImpl() {
  callback_.Reset();
  CleanUp();
}

void DNSClientImpl::CleanUp() {
  weak_factory_for_timeout_.InvalidateWeakPtrs();

  // Need to destroy listeners at first, and then call ares_destroy(), since the
  // latter may close fds.
  read_handlers_.clear();
  write_handlers_.clear();
  if (channel_ != nullptr) {
    ares_->destroy(channel_);
  }
  channel_ = nullptr;
}

// static
void DNSClientImpl::AresGetaddrinfoCallback(void* arg,
                                            int status,
                                            int /*timeouts*/,
                                            struct ares_addrinfo* result) {
  DNSClientImpl* res = static_cast<DNSClientImpl*>(arg);

  // Note that this function is called in the ares code path (and it will go
  // back to function which invokes the ares code path eventually) so it's
  // better to delayed the processing of the tasks in this function which can
  // affect the state of this object.
  res->ProcessGetaddrinfoCallback(status, result);
}

void DNSClientImpl::ProcessGetaddrinfoCallback(int status,
                                               struct ares_addrinfo* info) {
  base::ScopedClosureRunner free_info(base::BindOnce(
      [](AresInterface* ares, struct ares_addrinfo* info) {
        ares->freeaddrinfo(info);
      },
      ares_, info));

  if (!IsRunning()) {
    return;
  }

  auto now = base::TimeTicks::Now();
  if (status != ARES_SUCCESS) {
    ReportFailure(now, AresStatusToError(status));
    return;
  }

  // Note that ENODATA should be returned when there is no record for the
  // hostname, so empty list here means an error.
  auto addrs = GetIPsFromAddrinfo(family_, info);
  if (!addrs.empty()) {
    ReportSuccess(now, std::move(addrs));
  } else {
    ReportFailure(now, Error::kInternal);
  }
}

void DNSClientImpl::ReportSuccess(base::TimeTicks stop,
                                  std::vector<IPAddress> ip_addrs) {
  ScheduleStopAndInvokeCallback(stop, std::move(ip_addrs));
}

void DNSClientImpl::ReportFailure(base::TimeTicks stop, Error err) {
  ScheduleStopAndInvokeCallback(stop, base::unexpected(err));
}

void DNSClientImpl::ScheduleStopAndInvokeCallback(base::TimeTicks stop,
                                                  Result result) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DNSClientImpl::StopAndInvokeCallback,
                     weak_factory_.GetWeakPtr(),
                     base::BindOnce(std::move(callback_), stop - start_,
                                    std::move(result))));
}

void DNSClientImpl::StopAndInvokeCallback(base::OnceClosure cb_with_result) {
  CleanUp();

  // Invoke the callback at the last so this object can be destroyed in the
  // callback.
  std::move(cb_with_result).Run();
}

void DNSClientImpl::OnSocketReadable(int fd) {
  ProcessFd(fd, /*write_fd=*/ARES_SOCKET_BAD);
}

void DNSClientImpl::OnSocketWritable(int fd) {
  ProcessFd(/*read_fd=*/ARES_SOCKET_BAD, fd);
}

void DNSClientImpl::OnTimeout() {
  ProcessFd(/*read_fd=*/ARES_SOCKET_BAD, /*write_fd=*/ARES_SOCKET_BAD);
  RefreshTimeout();
}

void DNSClientImpl::ProcessFd(int read_fd, int write_fd) {
  read_handlers_.clear();
  write_handlers_.clear();
  ares_->process_fd(channel_, read_fd, write_fd);
  RefreshHandlers();
}

void DNSClientImpl::RefreshHandlers() {
  if (!IsRunning()) {
    return;
  }

  ares_socket_t sockets[ARES_GETSOCK_MAXNUM];
  int action_bits = ares_->getsock(channel_, sockets, ARES_GETSOCK_MAXNUM);

  for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(action_bits, i)) {
      read_handlers_.push_back(base::FileDescriptorWatcher::WatchReadable(
          sockets[i], base::BindRepeating(&DNSClientImpl::OnSocketReadable,
                                          base::Unretained(this), sockets[i])));
    }
    if (ARES_GETSOCK_WRITABLE(action_bits, i)) {
      write_handlers_.push_back(base::FileDescriptorWatcher::WatchWritable(
          sockets[i], base::BindRepeating(&DNSClientImpl::OnSocketWritable,
                                          base::Unretained(this), sockets[i])));
    }
  }
}

void DNSClientImpl::RefreshTimeout() {
  weak_factory_for_timeout_.InvalidateWeakPtrs();

  if (!IsRunning()) {
    return;
  }

  // Schedule timer event for the earlier of our timeout or one requested by
  // the resolver library.
  const auto now = base::TimeTicks::Now();
  if (now >= deadline_) {
    ReportFailure(now, Error::kTimedOut);
    return;
  }

  const base::TimeDelta max = deadline_ - now;
  struct timeval max_tv = {
      .tv_sec = static_cast<time_t>(max.InSeconds()),
      .tv_usec = static_cast<suseconds_t>(
          (max - base::Seconds(max.InSeconds())).InMicroseconds()),
  };
  struct timeval ret_tv;
  struct timeval* tv = ares_->timeout(channel_, &max_tv, &ret_tv);

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DNSClientImpl::OnTimeout,
                     weak_factory_for_timeout_.GetWeakPtr()),
      base::Seconds(tv->tv_sec) + base::Microseconds(tv->tv_usec));
}

}  // namespace

DNSClient::~DNSClient() = default;

std::unique_ptr<DNSClient> DNSClientFactory::Resolve(
    IPFamily family,
    std::string_view hostname,
    DNSClient::CallbackWithDuration callback,
    const DNSClient::Options& options,
    AresInterface* ares) {
  if (!ares) {
    ares = AresInterface::GetInstance();
  }
  return std::make_unique<DNSClientImpl>(family, hostname, std::move(callback),
                                         options, ares);
}

std::unique_ptr<DNSClient> DNSClientFactory::Resolve(
    IPFamily family,
    std::string_view hostname,
    DNSClient::Callback callback,
    const DNSClient::Options& options,
    AresInterface* ares) {
  auto wrapped_callback = base::BindOnce(
      [](DNSClient::Callback original_callback, base::TimeDelta duration,
         const DNSClient::Result& result) {
        std::move(original_callback).Run(result);
      },
      std::move(callback));
  return Resolve(family, hostname, std::move(wrapped_callback), options, ares);
}

// static
std::string_view DNSClient::ErrorName(DNSClient::Error error) {
  switch (error) {
    case Error::kInternal:
      return "InternalError";
    case Error::kNoData:
      return "NoData";
    case Error::kFormErr:
      return "FormError";
    case Error::kServerFail:
      return "ServerFailure";
    case Error::kNotFound:
      return "NotFound";
    case Error::kNotImplemented:
      return "NotImplemented";
    case Error::kRefused:
      return "Refused";
    case Error::kBadQuery:
      return "BadQuery";
    case Error::kBadName:
      return "BadName";
    case Error::kBadFamily:
      return "BadFamily";
    case Error::kBadResp:
      return "BadResp";
    case Error::kConnRefused:
      return "ConnectionRefused";
    case Error::kTimedOut:
      return "TimedOut";
    case Error::kEndOfFile:
      return "EndOfFile";
    case Error::kReadErr:
      return "FileReadError";
    case Error::kNoMemory:
      return "OutOfMemory";
    case Error::kChannelDestroyed:
      return "ChannelIsBeingDestroyed";
    case Error::kBadFormat:
      return "MisformattedInput";
    case Error::kBadFlags:
      return "IllegalFlagsSpecified";
    case Error::kBadHostname:
      return "HostnameWasNotNumeric";
    case Error::kBadHints:
      return "IllegalHintFlagsSpecified";
    case Error::kNotInit:
      return "LibraryNotInitialized";
    case Error::kLoadErr:
      return "LoadError";
    case Error::kGetNetworkParamsNotFound:
      return "GetNetworkParamsFunctionNotFound";
    case Error::kCancelled:
      return "QueryCancelled";
  }
}

std::ostream& operator<<(std::ostream& stream, DNSClient::Error error) {
  return stream << DNSClient::ErrorName(error);
}

}  // namespace net_base

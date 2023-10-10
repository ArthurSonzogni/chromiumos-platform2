// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/dns_client.h"

#include <netdb.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ares.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>

#include "net-base/ares_interface.h"
#include "net-base/ip_address.h"

namespace net_base {

namespace {

// Returns the IP addrs from |hostent|. Return empty vector on parsing failures.
std::vector<IPAddress> GetIPsFromHostent(IPFamily expected_family,
                                         const struct hostent* hostent) {
  std::vector<IPAddress> ret;
  const size_t expected_length = IPAddress::GetAddressLength(expected_family);

  if (!hostent) {
    LOG(ERROR) << "hostent is nullptr";
    return ret;
  }
  if (ToSAFamily(expected_family) != hostent->h_addrtype) {
    LOG(ERROR) << "IP family mismatched: expect " << expected_family << ", got "
               << hostent->h_addrtype;
    return ret;
  }
  if (static_cast<size_t>(hostent->h_length) != expected_length) {
    LOG(ERROR) << "IP address length mismatched: expect " << expected_length
               << ", got " << hostent->h_length;
    return ret;
  }

  // Iterate over h_addr_list to get the IP addresses.
  for (int i = 0; hostent->h_addr_list[i] != nullptr; i++) {
    auto addr = IPAddress::CreateFromBytes(
        {reinterpret_cast<unsigned char*>(hostent->h_addr_list[i]),
         expected_length});
    ret.push_back(addr.value());  // should always be valid
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
    case ARES_EBADNAME:
    case ARES_EBADFAMILY:
    case ARES_EBADRESP:
      return Error::kBadQuery;
    case ARES_ECONNREFUSED:
      return Error::kRefused;
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
                Callback callback,
                const Options& options,
                AresInterface* ares);
  ~DNSClientImpl();

 private:
  // Clean up the internal state.
  void CleanUp();

  // Callback from c-ares.
  static void AresGethostbynameCallback(void* arg,
                                        int status,
                                        int timeouts,
                                        struct hostent* hostent);
  void ProcessGethostbynameCallback(int status, struct hostent* hostent);

  // Helper functions to invoke the callback. Passing by values are expected
  // here since we need move or copy inside the functions.
  void ReportSuccess(std::vector<IPAddress> ip_addrs);
  void ReportFailure(Error err);
  void ScheduleStopAndInvokeCallback(Result result);
  void StopAndInvokeCallback(base::OnceClosure cb_with_result);

  // Helper functions for the fd management.
  // - OnSocketReadable(), OnSocketWritable(), and OnTimeout(): Event handlers
  //   called on the corresponding events. These three functions will called
  //   ProcessFd() inside.
  // - ProcessFd(): called ares_process_fd(), which may invoke the callback
  //   (i.e., AresGethostbynameCallback()).
  // - RefreshHandlersAndTimeout(): Set up the event handlers
  //   (OnSocketReadable(), OnSocketWritable(), and OnTimeout()).
  void OnSocketReadable(int fd);
  void OnSocketWritable(int fd);
  void OnTimeout();
  void ProcessFd(int read_fd, int write_fd);
  void RefreshHandlersAndTimeout();

  // Returns true if this object hasn't get the results.
  bool IsRunning() const { return !callback_.is_null(); }

  AresInterface* ares_;

  const IPFamily family_;
  const base::TimeDelta timeout_;

  ares_channel channel_ = nullptr;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      read_handlers_;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      write_handlers_;
  base::TimeTicks start_time_;

  Callback callback_;

  // For cancelling the ongoing timeout task.
  base::WeakPtrFactory<DNSClientImpl> weak_factory_for_timeout_{this};
  // The weak pointers created by this weak factory have the same lifetime with
  // the object.
  base::WeakPtrFactory<DNSClientImpl> weak_factory_{this};
};

DNSClientImpl::DNSClientImpl(IPFamily family,
                             std::string_view hostname,
                             Callback callback,
                             const Options& options,
                             AresInterface* ares)
    : ares_(ares),
      family_(family),
      timeout_(options.timeout),
      callback_(std::move(callback)) {
  struct ares_options ares_opts;
  memset(&ares_opts, 0, sizeof(ares_opts));

  constexpr static auto kMaxTimeout = base::Minutes(10);
  if (timeout_ > kMaxTimeout || timeout_.is_zero()) {
    LOG(ERROR) << "Invalid timeout: " << timeout_.InMilliseconds() << "ms";
    ReportFailure(Error::kInternal);
    return;
  }
  ares_opts.timeout = static_cast<int>(timeout_.InMilliseconds());

  int status = ares_->init_options(&channel_, &ares_opts, ARES_OPT_TIMEOUTMS);
  if (status != ARES_SUCCESS) {
    ReportFailure(AresStatusToError(status));
    return;
  }

  start_time_ = base::TimeTicks::Now();

  // The raw pointer here is safe since the callback can only be invoked inside
  // some c-ares functions, while they can only be called from this object.
  ares_->gethostbyname(channel_, std::string(hostname).c_str(),
                       net_base::ToSAFamily(family_), AresGethostbynameCallback,
                       this);

  RefreshHandlersAndTimeout();
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
void DNSClientImpl::AresGethostbynameCallback(void* arg,
                                              int status,
                                              int /*timeouts*/,
                                              struct hostent* hostent) {
  DNSClientImpl* res = static_cast<DNSClientImpl*>(arg);

  // Note that this function is called in the ares code path (and it will go
  // back to function which invokes the ares code path eventually) so it's
  // better to delayed the processing of the tasks in this function which can
  // affect the state of this object.
  res->ProcessGethostbynameCallback(status, hostent);
}

void DNSClientImpl::ProcessGethostbynameCallback(int status,
                                                 struct hostent* hostent) {
  if (!IsRunning()) {
    return;
  }

  if (status != ARES_SUCCESS) {
    ReportFailure(AresStatusToError(status));
    return;
  }

  // Note that ENODATA should be returned when there is no record for the
  // hostname, so empty list here means an error.
  auto addrs = GetIPsFromHostent(family_, hostent);
  if (!addrs.empty()) {
    ReportSuccess(std::move(addrs));
  } else {
    ReportFailure(Error::kInternal);
  }
}

void DNSClientImpl::ReportSuccess(std::vector<IPAddress> ip_addrs) {
  ScheduleStopAndInvokeCallback(std::move(ip_addrs));
}

void DNSClientImpl::ReportFailure(Error err) {
  ScheduleStopAndInvokeCallback(base::unexpected(err));
}

void DNSClientImpl::ScheduleStopAndInvokeCallback(Result result) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DNSClientImpl::StopAndInvokeCallback,
                     weak_factory_.GetWeakPtr(),
                     base::BindOnce(std::move(callback_), std::move(result))));
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
}

void DNSClientImpl::ProcessFd(int read_fd, int write_fd) {
  read_handlers_.clear();
  write_handlers_.clear();
  ares_->process_fd(channel_, read_fd, write_fd);
  // TODO(b/302101630): We don't need to reset timeout on ProcessFD(), only on
  // timeout callbacks.
  RefreshHandlersAndTimeout();
}

void DNSClientImpl::RefreshHandlersAndTimeout() {
  weak_factory_for_timeout_.InvalidateWeakPtrs();

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

  // TODO(b/302101630): The timeout logic below can be optimized. We can either
  // 1) trust the timeout from ares (only reset the timeout in the timeout
  //    handler), or
  // 2) only use the timeout logic here (instead of querying timeout in ares).

  // Schedule timer event for the earlier of our timeout or one requested by
  // the resolver library.
  const base::TimeDelta elapsed_time = base::TimeTicks::Now() - start_time_;
  if (elapsed_time >= timeout_) {
    ReportFailure(Error::kTimedOut);
    return;
  }

  const base::TimeDelta max = timeout_ - elapsed_time;
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

std::unique_ptr<DNSClient> DNSClient::Resolve(IPFamily family,
                                              std::string_view hostname,
                                              Callback callback,
                                              const Options& options,
                                              AresInterface* ares) {
  if (!ares) {
    ares = AresInterface::GetInstance();
  }
  return std::make_unique<DNSClientImpl>(family, hostname, std::move(callback),
                                         options, ares);
}

}  // namespace net_base
